/* -------------------------------------------------------------------------
 *
 * nodeGraphScan.cpp
 *	  Support routines for sequential scans of relations.
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 * Portions Copyright (c) 2021, openGauss Contributors
 *
 *
 * IDENTIFICATION
 *	  src/gausskernel/runtime/executor/nodeGraphScan.cpp
 *
 * -------------------------------------------------------------------------
 *
 * INTERFACE ROUTINES
 *		ExecGraphScan			sequentially scans a relation.
 *		ExecGraphNext			retrieve next tuple in sequential order.
 *		ExecInitGraphScan		creates and initializes a seqscan node.
 *		ExecEndGraphScan		releases any storage allocated.
 *		ExecReScanGraphScan		rescans the relation
 *		ExecGraphMarkPos		marks scan position
 *		ExecGraphRestrPos		restores scan position
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include "access/relscan.h"
#include "access/tableam.h"
#include "catalog/pg_partition_fn.h"
#include "commands/cluster.h"
#include "executor/exec/execdebug.h"
#include "executor/node/nodeModifyTable.h"
#include "executor/node/nodeSamplescan.h"
#include "executor/node/nodeGraphScan.h"
#include "pgxc/redistrib.h"
#include "miscadmin.h"
#include "storage/buf/bufmgr.h"
#include "storage/tcap.h"
#include "utils/guc.h"
#include "utils/rel.h"
#include "utils/rel_gs.h"
#include "utils/snapmgr.h"
#include "nodes/execnodes.h"
#include "nodes/makefuncs.h"
#include "optimizer/pruning.h"
#include "parser/parsetree.h"
#include "access/ustore/knl_uheap.h"
#include "access/ustore/knl_uscan.h"
#include "optimizer/var.h"
#include "optimizer/tlist.h"
#include "executor/node/nodeSeqscan.h"

static TupleTableSlot* ExecGraphScan(PlanState* state);
/* ----------------------------------------------------------------
 *						Scan Support
 * ----------------------------------------------------------------
 */
/* ----------------------------------------------------------------
 *		GraphNext
 *
 *		This is a workhorse for ExecSeqScan
 * ----------------------------------------------------------------
 */
TupleTableSlot* GraphNext(GraphScanState* node)
{
    return NULL;
}
/*
 * GraphRecheck -- access method routine to recheck a tuple in EvalPlanQual
 */
static bool GraphRecheck(GraphScanState* node, TupleTableSlot* slot)
{
    /*
     * Note that unlike IndexScan, GraphScan never use keys in heap_beginscan
     * (and this is very bad) - so, here we do not check are keys ok or not.
     */
    return true;
}

/* ----------------------------------------------------------------
 *		ExecGraphScan(node)
 *
 *		Scans the relation sequentially and returns the next qualifying
 *		tuple.
 *		We call the ExecScan() routine and pass it the appropriate
 *		access method functions.
 * ----------------------------------------------------------------
 */
static TupleTableSlot* ExecGraphScan(PlanState* state)
{
    //todo
    return NULL;
}

/* ----------------------------------------------------------------
 *		ExecInitGraphScan
 * ----------------------------------------------------------------
 */
GraphScanState* ExecInitGraphScan(GraphScan* node, EState* estate, int eflags)
{
    GraphScanState *scanstate = makeNode(GraphScanState);
    
    // 先执行SeqScanState初始化工作
    SeqScanState *ssState = ExecInitSeqScan(&(node->scan),estate,eflags);

    // 把执行器函数指针改回ExecGraphScan
    ssState->ps.ExecProcNode = ExecGraphScan;
    scanstate->ss = *ssState;

    // 传递MATCH信息到GraphScanState中
    scanstate->cypher_rels = node->cypher_rels;
    scanstate->cypher_restrictexprlist = node->cypher_restrictexprlist;

    return scanstate;
}

/* ----------------------------------------------------------------
 *		ExecEndGraphScan
 *
 *		frees any storage allocated through C routines.
 * ----------------------------------------------------------------
 */
void ExecEndGraphScan(GraphScanState* node)
{
   ExecEndSeqScan(&(node->ss));
}

/* ----------------------------------------------------------------
 *						Join Support
 * ----------------------------------------------------------------
 */
/* ----------------------------------------------------------------
 *		ExecReScanGraphScan
 *
 *		Rescans the relation.
 * ----------------------------------------------------------------
 */
void ExecReScanGraphScan(GraphScanState* node)
{
    return;
}

/* ----------------------------------------------------------------
 *		ExecGraphMarkPos(node)
 *
 *		Marks scan position.
 * ----------------------------------------------------------------
 */
void ExecGraphMarkPos(GraphScanState* node)
{
    return;
}

/* ----------------------------------------------------------------
 *		ExecGraphRestrPos
 *
 *		Restores scan position.
 * ----------------------------------------------------------------
 */
void ExecGraphRestrPos(GraphScanState* node)
{
    return;
}

