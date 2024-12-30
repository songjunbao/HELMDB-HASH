/* -------------------------------------------------------------------------
 *
 * nodeSort.h
 *
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeArrayScan.h
 *
 * snliao 2024.7.6
 * ArrayScan算子
 * -------------------------------------------------------------------------
 */
#ifndef NODEARRAYSCAN_H
#define NODEARRAYSCAN_H

#include "nodes/execnodes.h"

// 算子初始化
extern ArrayScanState* ExecInitArrayScan(ArrayScan* node, EState* estate, int eflags);

// 算子清理和结束
extern void ExecEndArrayScan(ArrayScanState* node);

// 从排好序的元组中构造数组
extern TupleTableSlot* extract_column_as_array(ArrayScanState *arrayScanState, char *colName);

// 仿照seqscan和sort算子的写法，把ExecArrayScan声明移到cpp文件里


#endif /* NODEARRAYSCAN.H */
