/* -------------------------------------------------------------------------
 *
 * nodeGraphscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeGraphscan.h
 *
 * -------------------------------------------------------------------------
 */
#ifndef NODEGRAPHSCAN_H
#define NODEGRAPHSCAN_H

#include "nodes/execnodes.h"

extern TupleTableSlot* GraphNext(GraphScanState* node);
extern GraphScanState* ExecInitGraphScan(GraphScan* node, EState* estate, int eflags);
extern void ExecEndGraphScan(GraphScanState* node);
extern void ExecGraphMarkPos(GraphScanState* node);
extern void ExecGraphRestrPos(GraphScanState* node);
extern void ExecReScanGraphScan(GraphScanState* node);
// extern void InitScanRelation(GraphScanState* node, EState* estate, int eflags);
// extern RangeScanInRedis reset_scan_qual(Relation currHeapRel, ScanState *node, bool isRangeScanInRedis = false);

// extern ExprState *ExecInitVecExpr(Expr *node, PlanState *parent);
#endif /* NODEGRAPHSCAN_H */
