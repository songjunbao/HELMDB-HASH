#include "postgres.h"

#include <float.h>
#include <math.h>

#include "access/amapi.h"
#include "commands/vacuum.h"
#include "access/hnsw.h"
#include "utils/guc.h"
#include "utils/selfuncs.h"

#if PG_VERSION_NUM >= 120000
#include "commands/progress.h"
#endif

int			hnsw_ef_search;
// static relopt_kind RELOPT_KIND_HNSW;

/*
 * Initialize index options and variables
 */
// void
// HnswInit(void)
// {
// 	hnsw_relopt_kind = add_reloption_kind();
// 	add_int_reloption(hnsw_relopt_kind, "m", "Max number of connections",
// 					  HNSW_DEFAULT_M, HNSW_MIN_M, HNSW_MAX_M
// #if PG_VERSION_NUM >= 130000
// 					  ,AccessExclusiveLock
// #endif
// 		);
// 	add_int_reloption(hnsw_relopt_kind, "ef_construction", "Size of the dynamic candidate list for construction",
// 					  HNSW_DEFAULT_EF_CONSTRUCTION, HNSW_MIN_EF_CONSTRUCTION, HNSW_MAX_EF_CONSTRUCTION
// #if PG_VERSION_NUM >= 130000
// 					  ,AccessExclusiveLock
// #endif
// 		);

// 	DefineCustomIntVariable("hnsw.ef_search", "Sets the size of the dynamic candidate list for search",reloptions
// 							"Valid range is 1..1000.", &hnsw_ef_search,
// 							HNSW_DEFAULT_EF_SEARCH, HNSW_MIN_EF_SEARCH, HNSW_MAX_EF_SEARCH, PGC_USERSET, 0, NULL, NULL, NULL);
// }

/*
 * Get the name of index build phase
 */
#if PG_VERSION_NUM >= 120000
static char *
hnswbuildphasename(int64 phasenum)
{
	switch (phasenum)
	{
		case PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE:
			return "initializing";
		case PROGRESS_HNSW_PHASE_LOAD:
			return "loading tuples";
		default:
			return NULL;
	}
}
#endif

/*
 * Estimate the cost of an index scan
 */
Datum hnswcostestimate(PG_FUNCTION_ARGS)
{
  PlannerInfo* root = (PlannerInfo*)PG_GETARG_POINTER(0);
	IndexPath* path = (IndexPath*)PG_GETARG_POINTER(1);
	double loop_count = PG_GETARG_FLOAT8(2);
	Cost* indexStartupCost = (Cost*)PG_GETARG_POINTER(3);
	Cost* indexTotalCost = (Cost*)PG_GETARG_POINTER(4);
	Selectivity* indexSelectivity = (Selectivity*)PG_GETARG_POINTER(5);
	double* indexCorrelation = (double*)PG_GETARG_POINTER(6);

	// GenericCosts costs;
	int			m;
	int			entryLevel;
	Relation	index;
#if PG_VERSION_NUM < 120000
	// List	   *qinfos;
#endif

	/* Never use index without order */
	if (path->indexorderbys == NULL)
	{
		*indexStartupCost = DBL_MAX;
		*indexTotalCost = DBL_MAX;
		*indexSelectivity = 0;
		*indexCorrelation = 0;
		// *indexPages = 0;
		PG_RETURN_VOID();
	}

	// MemSet(&costs, 0, sizeof(costs));

	index = index_open(path->indexinfo->indexoid, NoLock);
	m = HnswGetM(index);
	index_close(index, NoLock);

	/* Approximate entry level */
	entryLevel = (int) -log(1.0 / path->indexinfo->tuples) * HnswGetMl(m);

	/* TODO Improve estimate of visited tuples (currently underestimates) */
	/* Account for number of tuples (or entry level), m, and ef_search */
	double numIndexTuples = (entryLevel + 2) * m;

#if PG_VERSION_NUM >= 120000
	genericcostestimate(root, path, loop_count, &costs);
#else
	// qinfos = deconstruct_indexquals(path);
	genericcostestimate(root, path, loop_count, numIndexTuples, indexStartupCost, indexTotalCost, indexSelectivity, indexCorrelation);
#endif

	/* Use total cost since most work happens before first tuple is returned */
	// *indexStartupCost = costs.indexTotalCost;
	// *indexTotalCost = costs.indexTotalCost;
	// *indexSelectivity = costs.indexSelectivity;
	// *indexCorrelation = costs.indexCorrelation;
	// *indexPages = costs.numIndexPages;

  PG_RETURN_VOID();
}

/*
 * Parse and validate the reloptions
 */
Datum hnswoptions(PG_FUNCTION_ARGS)
{
  Datum reloptions = PG_GETARG_DATUM(0);
	bool validate = PG_GETARG_BOOL(1);

	static const relopt_parse_elt tab[] = {
		{"m", RELOPT_TYPE_INT, offsetof(HnswOptions, m)},
		{"ef_construction", RELOPT_TYPE_INT, offsetof(HnswOptions, efConstruction)},
	};

#if PG_VERSION_NUM >= 130000
	return (bytea *) build_reloptions(reloptions, validate,
									  hnsw_relopt_kind,
									  sizeof(HnswOptions),
									  tab, lengthof(tab));
#else
	relopt_value *options;
	int			numoptions;
	HnswOptions *rdopts;

	options = parseRelOptions(reloptions, validate, RELOPT_KIND_HNSW, &numoptions);
	rdopts = (HnswOptions *)allocateReloptStruct(sizeof(HnswOptions), options, numoptions);
	fillRelOptions((void *) rdopts, sizeof(HnswOptions), options, numoptions,
				   validate, tab, lengthof(tab));

	PG_RETURN_BYTEA_P((bytea *) rdopts);
#endif
}

/*
 * Validate catalog entries for the specified operator class
 */
static bool
hnswvalidate(Oid opclassoid)
{
	return true;
}

/*
 * Define index handler
 *
 * See https://www.postgresql.org/docs/current/index-api.html
 */
// Datum
// hnswhandler(PG_FUNCTION_ARGS)
// {
// 	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

// 	amroutine->amstrategies = 0;
// 	amroutine->amsupport = 2;
// #if PG_VERSION_NUM >= 130000
// 	amroutine->amoptsprocnum = 0;
// #endif
// 	amroutine->amcanorder = false;
// 	amroutine->amcanorderbyop = true;
// 	amroutine->amcanbackward = false;	/* can change direction mid-scan */
// 	amroutine->amcanunique = false;
// 	amroutine->amcanmulticol = false;
// 	amroutine->amoptionalkey = true;
// 	amroutine->amsearcharray = false;
// 	amroutine->amsearchnulls = false;
// 	amroutine->amstorage = false;
// 	amroutine->amclusterable = false;
// 	amroutine->ampredlocks = false;
// 	amroutine->amcanparallel = false;
// 	amroutine->amcaninclude = false;
// #if PG_VERSION_NUM >= 130000
// 	amroutine->amusemaintenanceworkmem = false; /* not used during VACUUM */
// 	amroutine->amparallelvacuumoptions = VACUUM_OPTION_PARALLEL_BULKDEL;
// #endif
// 	amroutine->amkeytype = InvalidOid;

// 	/* Interface functions */
// 	amroutine->ambuild = hnswbuild;
// 	amroutine->ambuildempty = hnswbuildempty;
// 	amroutine->aminsert = hnswinsert;
// 	amroutine->ambulkdelete = hnswbulkdelete;
// 	amroutine->amvacuumcleanup = hnswvacuumcleanup;
// 	amroutine->amcanreturn = NULL;	/* tuple not included in heapsort */
// 	amroutine->amcostestimate = hnswcostestimate;
// 	amroutine->amoptions = hnswoptions;
// 	amroutine->amproperty = NULL;	/* TODO AMPROP_DISTANCE_ORDERABLE */
// #if PG_VERSION_NUM >= 120000
// 	amroutine->ambuildphasename = hnswbuildphasename;
// #endif
// 	amroutine->amvalidate = hnswvalidate;
// #if PG_VERSION_NUM >= 140000
// 	amroutine->amadjustmembers = NULL;
// #endif
// 	amroutine->ambeginscan = hnswbeginscan;
// 	amroutine->amrescan = hnswrescan;
// 	amroutine->amgettuple = hnswgettuple;
// 	amroutine->amgetbitmap = NULL;
// 	amroutine->amendscan = hnswendscan;
// 	amroutine->ammarkpos = NULL;
// 	amroutine->amrestrpos = NULL;

// 	/* Interface functions to support parallel index scans */
// 	amroutine->amestimateparallelscan = NULL;
// 	amroutine->aminitparallelscan = NULL;
// 	amroutine->amparallelrescan = NULL;

// 	PG_RETURN_POINTER(amroutine);
// }
