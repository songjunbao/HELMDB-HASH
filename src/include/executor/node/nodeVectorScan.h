/* -------------------------------------------------------------------------
 *
 * nodeSort.h
 *
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeVectorScan.h
 *
 * snliao 2024.7.6
 * VectorScan算子
 * -------------------------------------------------------------------------
 */
#ifndef NODEVECTORSCAN_H
#define NODEVECTORSCAN_H

#include "nodes/execnodes.h"

// 算子初始化
extern VectorScanState* ExecInitVectorScan(VectorScan* node, EState* estate, int eflags);

// 算子清理和结束
extern void ExecEndVectorScan(VectorScanState* node);

// 仿照seqscan和sort算子的写法，把ExecVectorScan声明移到cpp文件里


#endif /* NODEVECTORSCAN.H */
