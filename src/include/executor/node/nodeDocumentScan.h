/* -------------------------------------------------------------------------
 *
 * nodeSort.h
 *
 *
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeDocumentScan.h
 *
 * snliao 2024.7.6
 * DocumentScan算子
 * -------------------------------------------------------------------------
 */
#ifndef NODEDOCUMENTSCAN_H
#define NODEDOCUMENTSCAN_H

#include "nodes/execnodes.h"

// 算子初始化
extern DocumentScanState* ExecInitDocumentScan(DocumentScan* node, EState* estate, int eflags);

// 算子清理和结束
extern void ExecEndDocumentScan(DocumentScanState* node);

// 仿照seqscan和sort算子的写法，把ExecDocumentScan声明移到cpp文件里


#endif /* NODEDOCUMENTSCAN.H */
