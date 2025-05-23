#include "postgres.h"

#include <float.h>

#include "access/relscan.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_type.h"
#include "access/ivfflat.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "storage/buf/bufmgr.h"

/*
 * Compare list distances
 */
static int
CompareLists(const pairingheap_node *a, const pairingheap_node *b, void *arg)
{
	if (((const IvfflatScanList *) a)->distance > ((const IvfflatScanList *) b)->distance)
		return 1;

	if (((const IvfflatScanList *) a)->distance < ((const IvfflatScanList *) b)->distance)
		return -1;

	return 0;
}

/*
 * Get lists and sort by distance
 */
static void
GetScanLists(IndexScanDesc scan, Datum value)
{
	Buffer		cbuf;
	Page		cpage;
	IvfflatList list;
	OffsetNumber offno;
	OffsetNumber maxoffno;
	BlockNumber nextblkno = IVFFLAT_HEAD_BLKNO;
	int			listCount = 0;
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	double		distance;
	IvfflatScanList *scanlist;
	double		maxDistance = DBL_MAX;

	/* Search all list pages */
	while (BlockNumberIsValid(nextblkno))
	{
		cbuf = ReadBuffer(scan->indexRelation, nextblkno);
		LockBuffer(cbuf, BUFFER_LOCK_SHARE);
		cpage = BufferGetPage(cbuf);

		maxoffno = PageGetMaxOffsetNumber(cpage);

		for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
		{
			list = (IvfflatList) PageGetItem(cpage, PageGetItemId(cpage, offno));

			/* Use procinfo from the index instead of scan key for performance */
			distance = DatumGetFloat8(FunctionCall2Coll(so->procinfo, so->collation, PointerGetDatum(&list->center), value));

			if (listCount < so->probes)
			{
				scanlist = &so->lists[listCount];
				scanlist->startPage = list->startPage;
				scanlist->distance = distance;
				listCount++;

				/* Add to heap */
				pairingheap_add(so->listQueue, &scanlist->ph_node);

				/* Calculate max distance */
				if (listCount == so->probes)
					maxDistance = ((IvfflatScanList *) pairingheap_first(so->listQueue))->distance;
			}
			else if (distance < maxDistance)
			{
				/* Remove */
				scanlist = (IvfflatScanList *) pairingheap_remove_first(so->listQueue);

				/* Reuse */
				scanlist->startPage = list->startPage;
				scanlist->distance = distance;
				pairingheap_add(so->listQueue, &scanlist->ph_node);

				/* Update max distance */
				maxDistance = ((IvfflatScanList *) pairingheap_first(so->listQueue))->distance;
			}
		}

		nextblkno = IvfflatPageGetOpaque(cpage)->nextblkno;

		UnlockReleaseBuffer(cbuf);
	}
}

/*
 * Get items
 */
static void
GetScanItems(IndexScanDesc scan, Datum value)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	Buffer		buf;
	Page		page;
	IndexTuple	itup;
	BlockNumber searchPage;
	OffsetNumber offno;
	OffsetNumber maxoffno;
	Datum		datum;
	bool		isnull;
	TupleDesc	tupdesc = RelationGetDescr(scan->indexRelation);
	double		tuples = 0;

#if PG_VERSION_NUM >= 120000
	TupleTableSlot *slot = MakeSingleTupleTableSlot(so->tupdesc, &TTSOpsVirtual);
#else
	TupleTableSlot *slot = MakeSingleTupleTableSlot(so->tupdesc);
#endif

	/*
	 * Reuse same set of shared buffers for scan
	 *
	 * See postgres/src/backend/storage/buffer/README for description
	 */
	BufferAccessStrategy bas = GetAccessStrategy(BAS_BULKREAD);

	/* Search closest probes lists */
	while (!pairingheap_is_empty(so->listQueue))
	{
		searchPage = ((IvfflatScanList *) pairingheap_remove_first(so->listQueue))->startPage;

		/* Search all entry pages for list */
		while (BlockNumberIsValid(searchPage))
		{
			buf = ReadBufferExtended(scan->indexRelation, MAIN_FORKNUM, searchPage, RBM_NORMAL, bas);
			LockBuffer(buf, BUFFER_LOCK_SHARE);
			page = BufferGetPage(buf);
			maxoffno = PageGetMaxOffsetNumber(page);

			for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
			{
				itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, offno));
				datum = index_getattr(itup, 1, tupdesc, &isnull);

				/*
				 * Add virtual tuple
				 *
				 * Use procinfo from the index instead of scan key for
				 * performance
				 */
				ExecClearTuple(slot);
				slot->tts_values[0] = FunctionCall2Coll(so->procinfo, so->collation, datum, value);
				slot->tts_isnull[0] = false;
				slot->tts_values[1] = PointerGetDatum(&itup->t_tid);
				slot->tts_isnull[1] = false;
				slot->tts_values[2] = Int32GetDatum((int) searchPage);
				slot->tts_isnull[2] = false;
				ExecStoreVirtualTuple(slot);

				tuplesort_puttupleslot(so->sortstate, slot);

				tuples++;
			}

			searchPage = IvfflatPageGetOpaque(page)->nextblkno;

			UnlockReleaseBuffer(buf);
		}
	}

	FreeAccessStrategy(bas);

	if (tuples < 100)
		ereport(DEBUG1,
				(errmsg("index scan found few tuples"),
				 errdetail("Index may have been created with little data."),
				 errhint("Recreate the index and possibly decrease lists.")));

	tuplesort_performsort(so->sortstate);
}

/*
 * Get dimensions from metapage
 */
static int
GetDimensions(Relation index)
{
	Buffer		buf;
	Page		page;
	IvfflatMetaPage metap;
	int			dimensions;

	buf = ReadBuffer(index, IVFFLAT_METAPAGE_BLKNO);
	LockBuffer(buf, BUFFER_LOCK_SHARE);
	page = BufferGetPage(buf);
	metap = IvfflatPageGetMeta(page);

	dimensions = metap->dimensions;

	UnlockReleaseBuffer(buf);

	return dimensions;
}

/*
 * Prepare for an index scan
 */
Datum ivfflatbeginscan(PG_FUNCTION_ARGS)
{
  Relation index = (Relation)PG_GETARG_POINTER(0);
	int nkeys = PG_GETARG_INT32(1);
	int norderbys = PG_GETARG_INT32(2);

	IndexScanDesc scan;
	IvfflatScanOpaque so;
	int			lists;
	AttrNumber	attNums[] = {1};
	Oid			sortOperators[] = {FLOAT8LTOID};
	Oid			sortCollations[] = {InvalidOid};
	bool		nullsFirstFlags[] = {false};
	int			probes = ivfflat_probes;

	scan = RelationGetIndexScan(index, nkeys, norderbys);
	lists = IvfflatGetLists(scan->indexRelation);

	if (probes > lists)
		probes = lists;

	so = (IvfflatScanOpaque) palloc(offsetof(IvfflatScanOpaqueData, lists) + probes * sizeof(IvfflatScanList));
	so->buf = InvalidBuffer;
	so->first = true;
	so->probes = probes;

	/* Set support functions */
	so->procinfo = index_getprocinfo(index, 1, IVFFLAT_DISTANCE_PROC);
	so->normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_NORM_PROC);
	so->collation = index->rd_indcollation[0];

	/* Create tuple description for sorting */
#if PG_VERSION_NUM >= 120000
	so->tupdesc = CreateTemplateTupleDesc(3);
#else
	so->tupdesc = CreateTemplateTupleDesc(3, false);
#endif
	TupleDescInitEntry(so->tupdesc, (AttrNumber) 1, "distance", FLOAT8OID, -1, 0);
	TupleDescInitEntry(so->tupdesc, (AttrNumber) 2, "tid", TIDOID, -1, 0);
	TupleDescInitEntry(so->tupdesc, (AttrNumber) 3, "indexblkno", INT4OID, -1, 0);

	/* Prep sort */
	so->sortstate = tuplesort_begin_heap(so->tupdesc, 1, attNums, sortOperators, sortCollations, nullsFirstFlags, u_sess->attr.attr_memory.work_mem, NULL, false);

#if PG_VERSION_NUM >= 120000
	so->slot = MakeSingleTupleTableSlot(so->tupdesc, &TTSOpsMinimalTuple);
#else
	so->slot = MakeSingleTupleTableSlot(so->tupdesc);
#endif

	so->listQueue = pairingheap_allocate(CompareLists, scan);

	scan->opaque = so;

	PG_RETURN_POINTER(scan);
}

/*
 * Start or restart an index scan
 */
Datum ivfflatrescan(PG_FUNCTION_ARGS)
{
  IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
	ScanKey keys = (ScanKey)PG_GETARG_POINTER(1);
	ScanKey orderbys = (ScanKey)PG_GETARG_POINTER(3);

	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

#if PG_VERSION_NUM >= 130000
	if (!so->first)
		tuplesort_reset(so->sortstate);
#endif

	so->first = true;
	pairingheap_reset(so->listQueue);

	if (keys && scan->numberOfKeys > 0)
		memmove(scan->keyData, keys, scan->numberOfKeys * sizeof(ScanKeyData));

	if (orderbys && scan->numberOfOrderBys > 0)
		memmove(scan->orderByData, orderbys, scan->numberOfOrderBys * sizeof(ScanKeyData));

  PG_RETURN_VOID();
}

/*
 * Fetch the next tuple in the given scan
 */
Datum ivfflatgettuple(PG_FUNCTION_ARGS)
{
  IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
	ScanDirection dir = (ScanDirection)PG_GETARG_INT32(1);

	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

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
			elog(ERROR, "cannot scan ivfflat index without order");

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
				IvfflatNormValue(so->normprocinfo, so->collation, &value, NULL);
		}

		IvfflatBench("GetScanLists", GetScanLists(scan, value));
		IvfflatBench("GetScanItems", GetScanItems(scan, value));
		so->first = false;

		/* Clean up if we allocated a new value */
		if (value != scan->orderByData->sk_argument)
			pfree(DatumGetPointer(value));
	}

	if (tuplesort_gettupleslot(so->sortstate, true, so->slot, NULL))
	{
		ItemPointer tid = (ItemPointer) DatumGetPointer(heap_slot_getattr(so->slot, 2, &so->isnull));
		BlockNumber indexblkno = DatumGetInt32(heap_slot_getattr(so->slot, 3, &so->isnull));

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

	PG_RETURN_BOOL(false);
}

/*
 * End a scan and release resources
 */
Datum ivfflatendscan(PG_FUNCTION_ARGS)
{
  IndexScanDesc scan = (IndexScanDesc)PG_GETARG_POINTER(0);
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	/* Release pin */
	if (BufferIsValid(so->buf))
		ReleaseBuffer(so->buf);

	pairingheap_free(so->listQueue);
	tuplesort_end(so->sortstate);

	pfree(so);
	scan->opaque = NULL;
  PG_RETURN_VOID();
}