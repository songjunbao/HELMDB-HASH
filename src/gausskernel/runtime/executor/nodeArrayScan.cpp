/* -------------------------------------------------------------------------
 *
 * nodeArrayScan.cpp
 * 
 * snliao 2024.7.6
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/gausskernel/runtime/executor/nodeArrayScan.cpp
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include "executor/exec/execdebug.h"
#include "executor/node/nodeArrayScan.h"
#include "executor/node/nodeDocumentScan.h"
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
#include "utils/array.h"
#include "catalog/pg_type.h"

#include "executor/node/nodeSeqscan.h"

#ifdef PGXC
#include "pgxc/pgxc.h"
#endif

//仿照seqscan和sort算子的写法，把ExecArrayScan声明写在cpp文件里
static TupleTableSlot* ExecArrayScan(PlanState* state);

/*
 * ArraySeqRecheck -- 仿照nodeSeqscan.cpp中的SeqRecheck编写，主要为传入ExecScan作为参数
 */
static bool ArraySeqRecheck(SeqScanState* node, TupleTableSlot* slot)
{
    /*
     * Note that unlike IndexScan, SeqScan never use keys in heap_beginscan
     * (and this is very bad) - so, here we do not check are keys ok or not.
     */
    return true;
}


//把排好序的元组转成数组
TupleTableSlot* extract_column_as_array(ArrayScanState *arrayScanState, char *colName) {
    //数组已构造完成(已经返回了，直接返回NULL结束执行)
    if(arrayScanState->arrayConstructed == true){
        return NULL;
    }
    // 获取原表元组描述符
    TupleDesc ori_tupdesc = arrayScanState->ss.ss_currentRelation->rd_att;
    int attnum = -1;
    Tuplesortstate* tuplesortstate = arrayScanState->tuplesortstate;

    // 创建一个新的 TupleTableSlot,设置槽的描述符为当前关系的描述符
    TupleTableSlot* temp_slot = MakeTupleTableSlot();
    ExecSetSlotDescriptor(temp_slot,ori_tupdesc);

    // 找到属性列下标(目前只考虑单个属性列的情况)
    for (int i = 0; i < ori_tupdesc->natts; i++) {
        if (strcmp(NameStr(ori_tupdesc->attrs[i].attname), colName) == 0) {
            attnum = i;
            break;
        }
    }

    if (attnum == -1) {
        elog(ERROR, "Column not found");
        return NULL;
    }

    // 创建数组存放列值
    Datum *values =(Datum*)palloc(sizeof(Datum) * (arrayScanState->array_size));
    bool *nulls = (bool*)palloc(sizeof(bool) *  (arrayScanState->array_size));

    //先把数组全部元素置为空
    for (int i = 0; i < arrayScanState->array_size; i++){
        values[i] = PointerGetDatum(NULL);
        nulls[i]  = true;
    }
    //再把非空的元素填上值
    for (int i = 0; i <  arrayScanState->tupleCount; i++) {
        (void)tuplesort_gettupleslot(arrayScanState->tuplesortstate, 
                            ScanDirectionIsForward(ForwardScanDirection), 
                            temp_slot, 
                            NULL);
        bool isNull = false;
        Datum attr_value = heap_slot_getattr(temp_slot, attnum + 1, &isNull);
        if(isNull == false){
            //计算下标
            int linear_index = 0;
            for(int t = 0; t < arrayScanState->dims_num; t++){
                if(t>0) linear_index *= arrayScanState->dim_sizes[t]; 
                Datum d = heap_slot_getattr(temp_slot, t+1, &isNull);
                linear_index += (DatumGetInt32(d) - arrayScanState->lower_bounds[t]);
            }
            values[linear_index] = attr_value;
            nulls[linear_index] = false;
        }
    }
    //释放temp_slot
    ExecDropSingleTupleTableSlot(temp_slot);

    // 构造数组
    int16 elmlen;
    bool elmbyval;
    char elmalign;
    get_typlenbyvalalign(ori_tupdesc->attrs[attnum].atttypid, &elmlen, &elmbyval, &elmalign);
    ArrayType *array = construct_md_array(values, nulls, arrayScanState->dims_num, arrayScanState->dim_sizes, arrayScanState->lower_bounds, 
                                                ori_tupdesc->attrs[attnum].atttypid, elmlen, elmbyval, elmalign);
    // 创建新元组描述符(用来存返回的数组)
    // 将数组放入一个新的TupleTableSlot
    TupleDesc res_tupdesc = arrayScanState->ss.ps.ps_ResultTupleSlot->tts_tupleDescriptor;
    TupleTableSlot *slot = MakeSingleTupleTableSlot(res_tupdesc);
    Datum arrayDatum = PointerGetDatum(array);
    bool nullFlag = false;
    
    ExecStoreAllNullTuple(slot);
    slot->tts_isnull[0] = false;
    slot->tts_values[0] = arrayDatum;

    pfree(values);
    pfree(nulls);

    arrayScanState->arrayConstructed = true;    //数组已构造完成

    return slot;
}



static TupleTableSlot* ExecArrayScan(PlanState* state)
{
    //先执行内部的顺序扫描操作
    ArrayScanState* arrayScanState = (ArrayScanState*)state;
    SeqScanState* ssnode = castNode(SeqScanState, &(arrayScanState->ss));
    EState* estate = arrayScanState->ss.ps.state;
    estate->es_direction = ForwardScanDirection;

    if(arrayScanState->sortDone == false){
    //循环地从下层的SeqScan节点读元组
        while(true){
            //从seqscan中读取1个元组
            TupleTableSlot* slot = ExecScan((ScanState *) ssnode, ssnode->ScanNextMtd, (ExecScanRecheckMtd) ArraySeqRecheck);

            //直到读完才退出
            if(TupIsNull(slot)){
                break;
            }

            //对过滤条件的处理
            //依次遍历每个维度，检查元组的值是否在上界和下界之间
            bool flag = true;
            for(int t=0; t<arrayScanState->dims_num; t++){
                bool isNull=false;
                Datum attrValue = heap_slot_getattr(slot, t+1, &isNull);
                if(isNull){
                    flag = false;
                    break;
                }
                int intValue = DatumGetInt32(attrValue);
                if(intValue < arrayScanState->lower_bounds[t] || intValue > arrayScanState->upper_bounds[t]){
                    flag = false;
                    break;
                } 
            }
            //不满足条件的元组直接跳过
            if(flag == false){
                continue;
            }

            //把满足条件的元组放入sorttuples数组内
            arrayScanState->tupleCount++;
            tuplesort_puttupleslot(arrayScanState->tuplesortstate, slot);
        }

        sort_count(arrayScanState->tuplesortstate);

        if (arrayScanState->ss.ps.instrument != NULL) {
            int64 peakMemorySize = (int64)tuplesort_get_peak_memory(arrayScanState->tuplesortstate);
            if (arrayScanState->ss.ps.instrument->memoryinfo.peakOpMemory < peakMemorySize)
                arrayScanState->ss.ps.instrument->memoryinfo.peakOpMemory = peakMemorySize;
        }

        //执行排序操作
        tuplesort_performsort(arrayScanState->tuplesortstate);

        arrayScanState->sortDone = true;
    }

    return extract_column_as_array(arrayScanState, (char*)lfirst(list_head(arrayScanState->attrs)));
}



//算子初始化
ArrayScanState* ExecInitArrayScan(ArrayScan* node, EState* estate, int eflags)
{
    ArrayScanState *arrayScanState = makeNode(ArrayScanState);

    //1.先执行SeqScanState初始化工作
    SeqScanState *ssState = ExecInitSeqScan(&(node->scan),estate,eflags);
    //把执行器函数指针改回ExecArrayScan
    ssState->ps.ExecProcNode = ExecArrayScan;
    arrayScanState->ss = *ssState;

    //创建结果元组描述符(用来存数组)
    TupleDesc resTupDesc = CreateTemplateTupleDesc(1, false);
    TupleDescInitEntry(resTupDesc, 1, "result_array", get_array_type(FLOAT8OID), -1, 0);
    BlessTupleDesc(resTupDesc);
    if(arrayScanState->ss.ps.ps_ResultTupleSlot != NULL){
     ExecDropSingleTupleTableSlot(arrayScanState->ss.ps.ps_ResultTupleSlot);    //先释放之前execInitSeqScan内初始化的元组描述符,防止内存泄漏
    }
    arrayScanState->ss.ps.ps_ResultTupleSlot = MakeSingleTupleTableSlot(resTupDesc);


    // //2. 执行tuplesortstate结构体的初始化 
    // //2.1 读取原表主键信息(也就是维度列的信息)
    Oid pk_index_oid = arrayScanState->ss.ss_currentRelation->rd_pkindex;
    if(OidIsValid(pk_index_oid) == false){
        elog(ERROR, "主键不合法\n");
    }

    arrayScanState->sortDone = false;      //排序完成标志改为false
    arrayScanState->arrayConstructed = false;     //数组还未构造

    Tuplesortstate *tupleSortState;     //元组排序结构体
    Relation index_rel = index_open(pk_index_oid, AccessShareLock); //索引关系表
    TupleDesc tupDesc = index_rel->rd_att;      //元组描述符
    int nKeys = index_rel->rd_index->indnatts;  //主键列数量
    AttrNumber*  attNums = (AttrNumber *) palloc(sizeof(AttrNumber) * nKeys);  // 用于存储每个键的属性号
    Oid* sortOperators = (Oid *) palloc(sizeof(Oid) * nKeys);  // 排序操作符数组
    Oid* sortCollations = (Oid *) palloc(sizeof(Oid) * nKeys);  // 排序规则数组
    bool* nullsFirstFlags = (bool *) palloc(sizeof(bool) * nKeys);  // NULLs 排序位置数组

    //填充排序所需信息
    for (int i = 0; i < nKeys; i++) {
        attNums[i] = index_rel->rd_index->indkey.values[i];
        Form_pg_attribute attr = TupleDescAttr(index_rel->rd_att, attNums[i] - 1);
        Oid l_oid = OpernameGetOprid(list_make1(makeString("<")), attr->atttypid, attr->atttypid);  //获取整数升序操作符oid
        sortOperators[i] = l_oid; // 使用适当的排序操作符
        sortCollations[i] = index_rel->rd_indcollation[i];
        nullsFirstFlags[i] = false;  // 默认排序方式，可以根据需要修改
    }

    //关闭索引表
    index_close(index_rel, AccessShareLock);

    int64 workMem = SET_NODEMEM(node->scan.plan.operatorMemKB[0], node->scan.plan.dop);
    int64 maxMem = (node->scan.plan.operatorMaxMem > 0) ? SET_NODEMEM(node->scan.plan.operatorMaxMem, node->scan.plan.dop) : 0;
    // 初始化 tuplesortstate
    tupleSortState = tuplesort_begin_heap(tupDesc,
                             nKeys, 
                             attNums,
                             sortOperators, 
                             sortCollations,
                             nullsFirstFlags,
                             workMem, 
                             false,
                             maxMem,
                             node->scan.plan.plan_node_id, 
                             SET_DOP(node->scan.plan.dop));
    
    arrayScanState->tuplesortstate = tupleSortState;

    //2.初始化查询基本信息
    //2.1 复制基本和标量类型
    arrayScanState->array_name = pstrdup(node->array_name);
    arrayScanState->dims_num = node->dims_num;
    arrayScanState->is_point_query = node->is_point_query;
    arrayScanState->attrs_num = node->attrs_num;
    arrayScanState->is_all_attrs = node->is_all_attrs;
    //2.2 初始化维度相关信息
    if(node->lower_bounds == NULL || 
        node->upper_bounds == NULL ||
        node->lower_bounds_valid == NULL ||
        node->lower_bounds_valid == NULL)
    {
        elog(ERROR, "维度相关列不能为空\n");
    }
    arrayScanState->lower_bounds = (int*) palloc(node->dims_num * sizeof(int));
    arrayScanState->upper_bounds = (int*) palloc(node->dims_num * sizeof(int));
    arrayScanState->lower_bounds_valid = (bool*) palloc(node->dims_num * sizeof(bool));
    arrayScanState->upper_bounds_valid = (bool*) palloc(node->dims_num * sizeof(bool));
    arrayScanState->dim_sizes = (int*) palloc(node->dims_num * sizeof(int));
    arrayScanState->array_size = 1;
    for(int i = 0; i < node->dims_num; i++){
        arrayScanState->lower_bounds[i] = node->lower_bounds[i];
        arrayScanState->upper_bounds[i] = node->upper_bounds[i];
        arrayScanState->lower_bounds_valid[i] = node->lower_bounds_valid[i];
        arrayScanState->upper_bounds_valid[i] = node->upper_bounds_valid[i];
        arrayScanState->dim_sizes[i] = node->upper_bounds[i] - node->lower_bounds[i] + 1;
        arrayScanState->array_size = arrayScanState->array_size * arrayScanState->dim_sizes[i];
    }
    //2.3 复制属性字符串数组
    if (node->attrs) {
        arrayScanState->attrs = NIL;
        ListCell *cell;
        foreach(cell, node->attrs) {
            char* data = (char*)lfirst(cell);
            arrayScanState->attrs = lappend(arrayScanState->attrs, strdup(data));
        }
    } else {
        arrayScanState->attrs = NULL;
    }

    return arrayScanState;
}


//TODO
//算子执行完成后的清理
void ExecEndArrayScan(ArrayScanState* node){

    //目前先直接执行顺序扫描的清理工作
    ExecEndSeqScan(&(node->ss));
}
