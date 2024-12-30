#include "postgres.h"

#include "access/relscan.h"
#include "access/hnsw.h"
#include "pgstat.h"
#include "storage/buf/bufmgr.h"
#include "storage/lmgr.h"
#include "utils/memutils.h"

/*
 * Algorithm 5 from paper
 */
static List *
GetScanItems(IndexScanDesc scan, Datum q)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	Relation	index = scan->indexRelation;
	FmgrInfo   *procinfo = so->procinfo;
	Oid			collation = so->collation;
	List	   *ep;
	List	   *w;
	HnswElement entryPoint = HnswGetEntryPoint(index);

	if (entryPoint == NULL)
		return NIL;

	ep = list_make1(HnswEntryCandidate(entryPoint, q, index, procinfo, collation, false));

	for (int lc = entryPoint->level; lc >= 1; lc--)
	{
		w = HnswSearchLayer(q, ep, 1, lc, index, procinfo, collation, false, NULL);
		ep = w;
	}

	return HnswSearchLayer(q, ep, hnsw_ef_search, 0, index, procinfo, collation, false, NULL);
}

/*
 * Get dimensions from metapage
 */
static int
GetDimensions(Relation index)
{
	Buffer		buf;
	Page		page;
	HnswMetaPage metap;
	int			dimensions;

	buf = ReadBuffer(index, HNSW_METAPAGE_BLKNO);
	LockBuffer(buf, BUFFER_LOCK_SHARE);
	page = BufferGetPage(buf);
	metap = HnswPageGetMeta(page);

	dimensions = metap->dimensions;

	UnlockReleaseBuffer(buf);

	return dimensions;
}

/*
 * Prepare for an index scan
 */
Datum hnswbeginscan(PG_FUNCTION_ARGS)
{
	Relation index = (Relation)PG_GETARG_POINTER(0);
	int nkeys = PG_GETARG_INT32(1);
	int norderbys = PG_GETARG_INT32(2);
	IndexScanDesc scan;
	HnswScanOpaque so;

	scan = RelationGetIndexScan(index, nkeys, norderbys);

	so = (HnswScanOpaque) palloc(sizeof(HnswScanOpaqueData));
	so->buf = InvalidBuffer;
	so->first = true;
	so->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
									   "Hnsw scan temporary context",
									   ALLOCSET_DEFAULT_SIZES);

	/* Set support functions */
	so->procinfo = index_getprocinfo(index, 1, HNSW_DISTANCE_PROC);
	so->normprocinfo = HnswOptionalProcInfo(index, HNSW_NORM_PROC);
	so->collation = index->rd_indcollation[0];

	scan->opaque = so;

	PG_RETURN_POINTER(scan);
}

/*
 * Start or restart an index scan
 */
Datum hnswrescan(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
	ScanKey keys = (ScanKey)PG_GETARG_POINTER(1);
	ScanKey orderbys = (ScanKey)PG_GETARG_POINTER(3);

	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	so->first = true;
	MemoryContextReset(so->tmpCtx);

	if (keys && scan->numberOfKeys > 0)
		memmove(scan->keyData, keys, scan->numberOfKeys * sizeof(ScanKeyData));

	if (orderbys && scan->numberOfOrderBys > 0)
		memmove(scan->orderByData, orderbys, scan->numberOfOrderBys * sizeof(ScanKeyData));
	
	PG_RETURN_VOID();
}

/*
 * Fetch the next tuple in the given scan
 */
Datum hnswgettuple(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
	ScanDirection dir = (ScanDirection)PG_GETARG_INT32(1);


	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	MemoryContext oldCtx = MemoryContextSwitchTo(so->tmpCtx);

	/*
	 * Index can be used to scan backward, but Postgres doesn't support
	 * backward scan on operators
	 */
	Assert(ScanDirectionIsForward(dir));

	if (so->first)
	{
		Datum		value;

		/* Count index scan for stats */
		pgstat_count_index_scan(scan->indexRelation);

		/* Safety check */
		if (scan->orderByData == NULL)
			elog(ERROR, "cannot scan hnsw index without order");

		if (scan->orderByData->sk_flags & SK_ISNULL)
			value = PointerGetDatum(InitVector(GetDimensions(scan->indexRelation)));
		else
		{
			value = scan->orderByData->sk_argument;

			/* Value should not be compressed or toasted */
			Assert(!VARATT_IS_COMPRESSED(DatumGetPointer(value)));
			Assert(!VARATT_IS_EXTENDED(DatumGetPointer(value)));

			/* Fine if normalization fails */
			if (so->normprocinfo != NULL)
				HnswNormValue(so->normprocinfo, so->collation, &value, NULL);
		}

		/*
		 * Get a shared lock. This allows vacuum to ensure no in-flight scans
		 * before marking tuples as deleted.
		 */
		LockPage(scan->indexRelation, HNSW_SCAN_LOCK, ShareLock);

		so->w = GetScanItems(scan, value);

		/* Release shared lock */
		UnlockPage(scan->indexRelation, HNSW_SCAN_LOCK, ShareLock);

		so->first = false;
	}

	while (list_length(so->w) > 0)
	{
		// elog(INFO,"====list length: %d ====",list_length(so->w));
		HnswCandidate *hc = (HnswCandidate *)llast(so->w);
		ItemPointer tid;
		BlockNumber indexblkno;

		/* Move to next element if no valid heap tids */
		if (list_length(hc->element->heaptids) == 0)
		{
			so->w = list_delete_last(so->w);
			continue;
		}

		tid = (ItemPointer)llast(hc->element->heaptids);
		indexblkno = hc->element->blkno;

		hc->element->heaptids = list_delete_last(hc->element->heaptids);

		MemoryContextSwitchTo(oldCtx);

#if PG_VERSION_NUM >= 120000
		scan->xs_heaptid = *tid;
#else
		scan->xs_ctup.t_self = *tid;
#endif

		if (BufferIsValid(so->buf))
			ReleaseBuffer(so->buf);

		/*
		 * An index scan must maintain a pin on the index page holding the
		 * item last returned by amgettuple
		 *
		 * https://www.postgresql.org/docs/current/index-locking.html
		 */
		so->buf = ReadBuffer(scan->indexRelation, indexblkno);

		// scan->xs_recheckorderby = false;
		PG_RETURN_BOOL(true);
	}

	MemoryContextSwitchTo(oldCtx);
	PG_RETURN_BOOL(false);
}

/*
 * End a scan and release resources
 */
Datum hnswendscan(PG_FUNCTION_ARGS)
{
	IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	/* Release pin */
	if (BufferIsValid(so->buf))
		ReleaseBuffer(so->buf);

	MemoryContextDelete(so->tmpCtx);

	pfree(so);
	scan->opaque = NULL;
	PG_RETURN_VOID();
}
