/* -------------------------------------------------------------------------
 *
 * nodeVectorScan.cpp
 * 
 * snliao 2024.7.6
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/gausskernel/runtime/executor/nodeVectorScan.cpp
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include "executor/exec/execdebug.h"
#include "executor/node/nodeVectorScan.h"
#include "miscadmin.h"
#include "optimizer/streamplan.h"
#include "pgstat.h"
#include "instruments/instr_unique_sql.h"
#include "utils/tuplesort.h"
#include "workload/workload.h"

#include "optimizer/var.h"
#include "optimizer/tlist.h"
#include "catalog/indexing.h"
#include "catalog/pg_index.h"

#include "utils/relcache.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/rel.h"
#include "utils/fmgroids.h"
#include "access/heapam.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_attribute.h"
#include "storage/lock/lock.h"

#include "access/tupdesc.h"
#include "executor/tuptable.h"
#include "catalog/pg_type.h"

#include "executor/node/nodeSeqscan.h"

#ifdef PGXC
#include "pgxc/pgxc.h"
#endif

//仿照seqscan和sort算子的写法，把ExecVectorScan声明写在cpp文件里
static TupleTableSlot* ExecVectorScan(PlanState* state);

/*
 * VectorSeqRecheck -- 仿照nodeSeqscan.cpp中的SeqRecheck编写，主要为传入ExecScan作为参数
 */
static bool VectorSeqRecheck(SeqScanState* node, TupleTableSlot* slot)
{
    /*
     * Note that unlike IndexScan, SeqScan never use keys in heap_beginscan
     * (and this is very bad) - so, here we do not check are keys ok or not.
     */
    return true;
}


static TupleTableSlot* ExecVectorScan(PlanState* state)
{
    //先执行内部的顺序扫描操作
    VectorScanState* vectorScanState = (VectorScanState*)state;
    SeqScanState* ssnode = castNode(SeqScanState, &(vectorScanState->ss));
    EState* estate = vectorScanState->ss.ps.state;
    estate->es_direction = ForwardScanDirection;

    return ExecScan((ScanState *) ssnode, ssnode->ScanNextMtd, (ExecScanRecheckMtd) VectorSeqRecheck);
}



//算子初始化
VectorScanState* ExecInitVectorScan(VectorScan* node, EState* estate, int eflags)
{
    VectorScanState *vectorScanState = makeNode(VectorScanState);

    //1.先执行SeqScanState初始化工作
    SeqScanState *ssState = ExecInitSeqScan(&(node->scan),estate,eflags);
    //把执行器函数指针改回ExecVectorScan
    ssState->ps.ExecProcNode = ExecVectorScan;
    vectorScanState->ss = *ssState;

    return vectorScanState;
}


//TODO
//算子执行完成后的清理
void ExecEndVectorScan(VectorScanState* node){

    //目前先直接执行顺序扫描的清理工作
    ExecEndSeqScan(&(node->ss));
}
