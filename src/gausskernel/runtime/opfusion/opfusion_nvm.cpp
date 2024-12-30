
/*
 * Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 *
 * openGauss is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ---------------------------------------------------------------------------------------
 *
 * opfusion_nvm.cpp
 *        Definition of nvm's operator for bypass executor.
 *
 * IDENTIFICATION
 *        src/gausskernel/runtime/opfusion/opfusion_nvm.cpp
 *
 * ---------------------------------------------------------------------------------------
 */

#include "opfusion/opfusion_nvm.h"
#include "parser/parse_coerce.h"

extern void *NvmGetFdwState(ForeignScan *fscan, Oid tableId);
extern int NvmSqlByPassQuery(ForeignScan *fscan, TupleDesc desc, void *state, TupleTableSlot *slot,
                             ParamListInfo paramList, void **index);
extern void NvmIterDelete(void *iterPar);
extern int NvmSqlByPassUpdate(ForeignScan *fscan, TupleDesc desc, void *state, TupleTableSlot *slot,
                              ParamListInfo paramList, NvmUpdateFusion *ptr);
extern int NvmSqlByPassInsert(Oid tableId, TupleDesc tupDesc, Datum *value, bool *isnull);
extern int NvmSqlByPassDelete(ForeignScan *fscan, TupleDesc desc, void *state, ParamListInfo paramList);

NvmSelectFusion::NvmSelectFusion(MemoryContext context, CachedPlanSource *psrc, List *plantree_list,
                                 ParamListInfo params)
    : OpFusion(context, psrc, plantree_list)
{
    MemoryContext old_context = nullptr;
    if (!IsGlobal()) {
        old_context = MemoryContextSwitchTo(m_global->m_context);
        InitGlobals();
        MemoryContextSwitchTo(old_context);
    }
    old_context = MemoryContextSwitchTo(m_local.m_localContext);
    InitLocals(params);
    MemoryContextSwitchTo(old_context);
}

void NvmSelectFusion::InitGlobals()
{
    if (m_global->m_planstmt->planTree->targetlist) {
        Plan *top_plan = m_global->m_planstmt->planTree;
        if (IsA(top_plan, Limit)) {
            Limit *limit = (Limit *)m_global->m_planstmt->planTree;
            if (limit->limitCount != nullptr && IsA(limit->limitCount, Const) &&
                !((Const *)limit->limitCount)->constisnull) {
                m_limitCount = DatumGetInt64(((Const *)limit->limitCount)->constvalue);
            }
            if (limit->limitOffset != nullptr && IsA(limit->limitOffset, Const) &&
                !((Const *)limit->limitOffset)->constisnull) {
                m_limitOffset = DatumGetInt64(((Const *)limit->limitOffset)->constvalue);
            }
            top_plan = top_plan->lefttree;
        }

        if (IsA(top_plan, LockRows)) {
            top_plan = top_plan->lefttree;
            m_isUpdate = true;
        }

        Assert(IsA(top_plan, ForeignScan));
        m_scan = (ForeignScan *)top_plan;

        m_global->m_tupDesc = ExecCleanTypeFromTL(m_global->m_planstmt->planTree->targetlist, false);
        m_global->m_reloid = getrelid(m_scan->scan.scanrelid, m_global->m_planstmt->rtable);
        Relation rel = heap_open(m_global->m_reloid, AccessShareLock);
        m_global->m_natts = RelationGetDescr(rel)->natts;
        m_TupDescOrg = CreateTupleDescCopy(RelationGetDescr(rel));
        heap_close(rel, AccessShareLock);
        m_nvmFdwState = NvmGetFdwState(m_scan, m_global->m_reloid);
        m_local.m_position = 0;
        m_local.m_isCompleted = false;
    } else {
        ereport(ERROR, (errmodule(MOD_EXECUTOR), errcode(ERRCODE_UNRECOGNIZED_NODE_TYPE),
                        errmsg("unrecognized node type: %d when executing executor node.",
                               (int)nodeTag(m_global->m_planstmt->planTree))));
    }
}

void NvmSelectFusion::InitLocals(ParamListInfo params)
{
    initParams(params);
    m_local.m_receiver = nullptr;
    m_local.m_isInsideRec = true;
    m_local.m_reslot = MakeSingleTupleTableSlot(m_global->m_tupDesc);
}

bool NvmSelectFusion::execute(long max_rows, char *completionTag)
{
    bool success = false;
    unsigned long nprocessed = m_nprocessed;
    int rc = 0;
    ParamListInfo param_list = m_local.m_outParams != nullptr ? m_local.m_outParams : m_local.m_params;
    int64 start_row = 0;
    int64 get_rows = 0;

    start_row = m_limitOffset >= 0 ? m_limitOffset : start_row;
    get_rows = m_limitCount >= 0 ? (m_limitCount + start_row) : max_rows;

    setReceiver();

    while (true) {
        rc = NvmSqlByPassQuery(m_scan, m_TupDescOrg, m_nvmFdwState, m_local.m_reslot, param_list, &m_iter);
        CHECK_FOR_INTERRUPTS();
        if (rc == 0) {
            if (nprocessed >= (unsigned long)get_rows) {
                if (max_rows == 0 || nprocessed < (unsigned long)max_rows) {
                    m_local.m_isCompleted = true;
                }
                break;
            }

            if (nprocessed >= (unsigned long)start_row) {
                (*m_local.m_receiver->receiveSlot)(m_local.m_reslot, m_local.m_receiver);
            }

            nprocessed++;
        } else {
            m_local.m_isCompleted = true;
            break;
        }
    }

    success = true;
    m_nprocessed = nprocessed;

    if (m_local.m_portalName == nullptr || m_local.m_portalName[0] == '\0') {
        m_local.m_isCompleted = true;
    }

    if (m_local.m_isInsideRec) {
        (*m_local.m_receiver->rDestroy)(m_local.m_receiver);
    }

    if (m_local.m_isCompleted) {
        NvmIterDelete(m_iter);
        m_iter = nullptr;
        m_nprocessed = 0;
    }

    errno_t errorno =
        snprintf_s(completionTag, COMPLETION_TAG_BUFSIZE, COMPLETION_TAG_BUFSIZE - 1, "SELECT %lu", nprocessed);
    securec_check_ss(errorno, "\0", "\0");

    return success;
}

NvmUpdateFusion::NvmUpdateFusion(MemoryContext context, CachedPlanSource *psrc, List *plantree_list,
                                 ParamListInfo params)
    : OpFusion(context, psrc, plantree_list)
{
    MemoryContext old_context = nullptr;
    if (!IsGlobal()) {
        old_context = MemoryContextSwitchTo(m_global->m_context);
        InitGlobals();
        MemoryContextSwitchTo(old_context);
    } else {
        m_c_global = ((NvmUpdateFusion *)(psrc->opFusionObj))->m_c_global;
    }
    old_context = MemoryContextSwitchTo(m_local.m_localContext);
    InitLocals(params);
    MemoryContextSwitchTo(old_context);
}

void NvmUpdateFusion::UpdateTargetResult(TupleTableSlot *reslot, bool *colMayChange)
{
    ParamListInfo parms = (m_local.m_outParams != nullptr ? m_local.m_outParams : m_local.m_params);

    Datum *values = reslot->tts_values;
    bool *isnull = reslot->tts_isnull;

    for (int i = 0; i < m_global->m_tupDesc->natts; i++) {
        m_c_local.m_curVarValue[i] = values[i];
        m_c_local.m_curVarIsnull[i] = isnull[i];
    }

    if (m_c_global->m_varNum > 0) {
        for (int i = 0; i < m_c_global->m_varNum; i++) {
            if (m_c_global->m_targetVarLoc[i].scanKeyIndex == m_c_global->m_targetVarLoc[i].varNo - 1) {
                colMayChange[m_c_global->m_targetVarLoc[i].scanKeyIndex] = true;
            }
            values[m_c_global->m_targetVarLoc[i].scanKeyIndex] =
                m_c_local.m_curVarValue[m_c_global->m_targetVarLoc[i].varNo - 1];
            isnull[m_c_global->m_targetVarLoc[i].scanKeyIndex] =
                m_c_local.m_curVarIsnull[m_c_global->m_targetVarLoc[i].varNo - 1];
        }
    }

    if (m_c_global->m_targetParamNum > 0) {
        for (int i = 0; i < m_c_global->m_targetParamNum; i++) {
            colMayChange[m_c_global->m_targetParamLoc[i].scanKeyIndx] = true;
            values[m_c_global->m_targetParamLoc[i].scanKeyIndx] =
                parms->params[m_c_global->m_targetParamLoc[i].paramId - 1].value;
            isnull[m_c_global->m_targetParamLoc[i].scanKeyIndx] =
                parms->params[m_c_global->m_targetParamLoc[i].paramId - 1].isnull;
        }
    }

    for (int i = 0; i < m_c_global->m_targetConstNum; i++) {
        if (m_c_global->m_targetConstLoc[i].constLoc >= 0) {
            colMayChange[m_c_global->m_targetConstLoc[i].constLoc] = true;
            values[m_c_global->m_targetConstLoc[i].constLoc] = m_c_global->m_targetConstLoc[i].constValue;
            isnull[m_c_global->m_targetConstLoc[i].constLoc] = m_c_global->m_targetConstLoc[i].constIsNull;
        }
    }

    for (int i = 0; i < m_c_global->m_targetFuncNum; ++i) {
        ELOG_FIELD_NAME_START(m_c_global->m_targetFuncNodes[i].resname);
        if (m_c_global->m_targetFuncNodes[i].funcid != InvalidOid) {
            bool func_isnull = false;
            colMayChange[m_c_global->m_targetFuncNodes[i].resno - 1] = true;
            values[m_c_global->m_targetFuncNodes[i].resno - 1] =
                CalFuncNodeVal(m_c_global->m_targetFuncNodes[i].funcid, m_c_global->m_targetFuncNodes[i].args,
                               &func_isnull, m_c_local.m_curVarValue, m_c_local.m_curVarIsnull);
        }

        ELOG_FIELD_NAME_END;
    }
}

void NvmUpdateFusion::InitGlobals()
{
    int hash_col_num = 0;
    m_c_global = (UpdateFusionGlobalVariable *)palloc0(sizeof(UpdateFusionGlobalVariable));

    ModifyTable *node = (ModifyTable *)m_global->m_planstmt->planTree;
    ForeignScan *scan = (ForeignScan *)linitial(node->plans);
    m_global->m_reloid =
        getrelid(linitial_int((List *)linitial(m_global->m_planstmt->resultRelations)), m_global->m_planstmt->rtable);

    Relation rel = heap_open(m_global->m_reloid, AccessShareLock);
    m_global->m_natts = RelationGetDescr(rel)->natts;
    m_global->m_tupDesc = CreateTupleDescCopy(RelationGetDescr(rel));
    hash_col_num = rel->rd_isblockchain ? 1 : 0;
    heap_close(rel, AccessShareLock);

    Assert(m_global->m_natts + 1 <= list_length(scan->scan.plan.targetlist) + hash_col_num);

    m_c_global->m_targetParamNum = 0;
    m_c_global->m_targetParamLoc = (ParamLoc *)palloc0(m_global->m_natts * sizeof(ParamLoc));
    m_c_global->m_targetConstLoc = (ConstLoc *)palloc0(m_global->m_natts * sizeof(ConstLoc));
    m_c_global->m_targetVarLoc = (VarLoc *)palloc0(m_global->m_natts * sizeof(VarLoc));
    m_c_global->m_targetFuncNum = 0;
    m_c_global->m_targetFuncNodes = (FuncExprInfo *)palloc0(m_global->m_natts * sizeof(FuncExprInfo));
    m_c_global->m_varNum = 0;
    m_c_global->m_scan = scan;

    int i = 0;
    ListCell *lc = nullptr;
    OpExpr *opexpr = nullptr;
    FuncExpr *func = nullptr;
    Expr *expr = nullptr;

    foreach (lc, scan->scan.plan.targetlist) {
        if (i >= m_global->m_natts) {
            break;
        }

        TargetEntry *res = (TargetEntry *)lfirst(lc);
        expr = res->expr;

        while (IsA(expr, RelabelType)) {
            expr = ((RelabelType *)expr)->arg;
        }

        m_c_global->m_targetConstLoc[i].constLoc = -1;
        if (IsA(expr, Param)) {
            Param *param = (Param *)expr;
            m_c_global->m_targetParamLoc[m_c_global->m_targetParamNum].paramId = param->paramid;
            m_c_global->m_targetParamLoc[m_c_global->m_targetParamNum++].scanKeyIndx = i;
        } else if (IsA(expr, Const)) {
            m_c_global->m_targetConstLoc[i].constValue = ((Const *)expr)->constvalue;
            m_c_global->m_targetConstLoc[i].constIsNull = ((Const *)expr)->constisnull;
            m_c_global->m_targetConstLoc[i].constLoc = i;
        } else if (IsA(expr, Var)) {
            Var *var = (Var *)expr;
            m_c_global->m_targetVarLoc[m_c_global->m_varNum].varNo = var->varattno;
            m_c_global->m_targetVarLoc[m_c_global->m_varNum++].scanKeyIndex = i;
        } else if (IsA(expr, OpExpr)) {
            opexpr = (OpExpr *)expr;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resno = res->resno;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resname = res->resname;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].funcid = opexpr->opfuncid;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].args = opexpr->args;
            ++m_c_global->m_targetFuncNum;
        } else if (IsA(expr, FuncExpr)) {
            func = (FuncExpr *)expr;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resno = res->resno;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resname = res->resname;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].funcid = func->funcid;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].args = func->args;
            ++m_c_global->m_targetFuncNum;
        }
        i++;
    }
    m_c_global->m_targetConstNum = i;
}

void NvmUpdateFusion::InitLocals(ParamListInfo params)
{
    m_local.m_tmpisnull = nullptr;
    m_local.m_tmpvals = nullptr;
    m_c_local.m_nvmFdwState = NvmGetFdwState(m_c_global->m_scan, m_global->m_reloid);

    m_local.m_reslot = MakeSingleTupleTableSlot(m_global->m_tupDesc);

    m_c_local.m_curVarValue = (Datum *)palloc0(m_global->m_natts * sizeof(Datum));
    m_c_local.m_curVarIsnull = (bool *)palloc0(m_global->m_natts * sizeof(bool));

    m_local.m_outParams = params;
    initParams(params);

    m_local.m_receiver = nullptr;
    m_local.m_isInsideRec = true;
    m_local.m_optype = UPDATE_FUSION;
}

bool NvmUpdateFusion::execute(long max_rows, char *completionTag)
{
    bool success = false;
    unsigned long nprocessed = 0;
    int rc = 0;
    errno_t errorno = EOK;
    ParamListInfo param_list = m_local.m_outParams != nullptr ? m_local.m_outParams : m_local.m_params;

    rc = NvmSqlByPassUpdate(m_c_global->m_scan, m_global->m_tupDesc, m_c_local.m_nvmFdwState, m_local.m_reslot,
                            param_list, this);
    if (rc >= 0) {
        nprocessed = rc;
        success = true;
        errorno =
            snprintf_s(completionTag, COMPLETION_TAG_BUFSIZE, COMPLETION_TAG_BUFSIZE - 1, "UPDATE %ld", nprocessed);
        securec_check_ss(errorno, "\0", "\0");
    }

    m_local.m_isCompleted = true;

    return success;
}

void NvmInsertFusion::InitGlobals()
{
    m_c_global = (InsertFusionGlobalVariable *)palloc0(sizeof(InsertFusionGlobalVariable));

    m_global->m_reloid =
        getrelid(linitial_int((List *)linitial(m_global->m_planstmt->resultRelations)), m_global->m_planstmt->rtable);
    ModifyTable *node = (ModifyTable *)m_global->m_planstmt->planTree;
    BaseResult *baseresult = (BaseResult *)linitial(node->plans);
    List *targetList = baseresult->plan.targetlist;

    Relation rel = heap_open(m_global->m_reloid, AccessShareLock);
    m_global->m_natts = RelationGetDescr(rel)->natts;
    m_global->m_tupDesc = CreateTupleDescCopy(RelationGetDescr(rel));
    heap_close(rel, AccessShareLock);

    m_global->m_paramNum = 0;
    m_global->m_paramLoc = (ParamLoc *)palloc0(m_global->m_natts * sizeof(ParamLoc));
    m_c_global->m_targetParamNum = 0;
    m_c_global->m_targetFuncNum = 0;
    m_c_global->m_targetFuncNodes = (FuncExprInfo *)palloc0(m_global->m_natts * sizeof(FuncExprInfo));
    m_c_global->m_targetConstNum = 0;
    m_c_global->m_targetConstLoc = (ConstLoc *)palloc0(m_global->m_natts * sizeof(ConstLoc));

    ListCell *lc = nullptr;
    int i = 0;
    FuncExpr *func = nullptr;
    TargetEntry *res = nullptr;
    Expr *expr = nullptr;
    OpExpr *opexpr = nullptr;

    foreach (lc, targetList) {
        res = (TargetEntry *)lfirst(lc);
        expr = res->expr;
        Assert(IsA(expr, Const) || IsA(expr, Param) || IsA(expr, FuncExpr) || IsA(expr, RelabelType) ||
               IsA(expr, OpExpr));

        while (IsA(expr, RelabelType)) {
            expr = ((RelabelType *)expr)->arg;
        }

        m_c_global->m_targetConstLoc[i].constLoc = -1;
        if (IsA(expr, FuncExpr)) {
            func = (FuncExpr *)expr;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resno = res->resno;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resname = res->resname;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].funcid = func->funcid;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].args = func->args;
            ++m_c_global->m_targetFuncNum;
        } else if (IsA(expr, Param)) {
            Param *param = (Param *)expr;
            m_global->m_paramLoc[m_c_global->m_targetParamNum].paramId = param->paramid;
            m_global->m_paramLoc[m_c_global->m_targetParamNum++].scanKeyIndx = i;
        } else if (IsA(expr, Const)) {
            m_c_global->m_targetConstLoc[i].constValue = ((Const *)expr)->constvalue;
            m_c_global->m_targetConstLoc[i].constIsNull = ((Const *)expr)->constisnull;
            m_c_global->m_targetConstLoc[i].constLoc = i;
        } else if (IsA(expr, OpExpr)) {
            opexpr = (OpExpr *)expr;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resno = res->resno;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].resname = res->resname;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].funcid = opexpr->opfuncid;
            m_c_global->m_targetFuncNodes[m_c_global->m_targetFuncNum].args = opexpr->args;
            ++m_c_global->m_targetFuncNum;
        }
        i++;
    }
    m_c_global->m_targetConstNum = i;
}

void NvmInsertFusion::InitLocals(ParamListInfo params)
{
    m_local.m_reslot = MakeSingleTupleTableSlot(m_global->m_tupDesc);
    if (m_global->m_table_type == TAM_USTORE) {
        m_local.m_reslot->tts_tupslotTableAm = TAM_USTORE;
    }
    m_local.m_values = (Datum *)palloc0(m_global->m_natts * sizeof(Datum));
    m_local.m_isnull = (bool *)palloc0(m_global->m_natts * sizeof(bool));
    m_c_local.m_curVarValue = (Datum *)palloc0(m_global->m_natts * sizeof(Datum));
    m_c_local.m_curVarIsnull = (bool *)palloc0(m_global->m_natts * sizeof(bool));
    initParams(params);
    m_local.m_receiver = nullptr;
    m_local.m_isInsideRec = true;
    m_local.m_optype = INSERT_FUSION;
}

NvmInsertFusion::NvmInsertFusion(MemoryContext context, CachedPlanSource *psrc, List *plantree_list,
                                 ParamListInfo params)
    : OpFusion(context, psrc, plantree_list)
{
    MemoryContext old_context = nullptr;
    if (!IsGlobal()) {
        old_context = MemoryContextSwitchTo(m_global->m_context);
        InitGlobals();
        MemoryContextSwitchTo(old_context);
    } else {
        m_c_global = ((NvmInsertFusion *)(psrc->opFusionObj))->m_c_global;
    }
    old_context = MemoryContextSwitchTo(m_local.m_localContext);
    InitLocals(params);
    MemoryContextSwitchTo(old_context);
}

void NvmInsertFusion::GetParameter()
{
    ParamListInfo params = m_local.m_outParams != nullptr ? m_local.m_outParams : m_local.m_params;
    bool func_isnull = false;

    for (int i = 0; i < m_global->m_tupDesc->natts; i++) {
        m_c_local.m_curVarValue[i] = m_local.m_values[i];
        m_c_local.m_curVarIsnull[i] = m_local.m_isnull[i];
    }

    for (int i = 0; i < m_c_global->m_targetConstNum; i++) {
        if (m_c_global->m_targetConstLoc[i].constLoc >= 0) {
            m_local.m_values[m_c_global->m_targetConstLoc[i].constLoc] = m_c_global->m_targetConstLoc[i].constValue;
            m_local.m_isnull[m_c_global->m_targetConstLoc[i].constLoc] = m_c_global->m_targetConstLoc[i].constIsNull;
        }
    }

    for (int i = 0; i < m_c_global->m_targetFuncNum; i++) {
        ELOG_FIELD_NAME_START(m_c_global->m_targetFuncNodes[i].resname);
        if (m_c_global->m_targetFuncNodes[i].funcid != InvalidOid) {
            func_isnull = false;
            m_local.m_values[m_c_global->m_targetFuncNodes[i].resno - 1] =
                CalFuncNodeVal(m_c_global->m_targetFuncNodes[i].funcid, m_c_global->m_targetFuncNodes[i].args,
                               &func_isnull, m_c_local.m_curVarValue, m_c_local.m_curVarIsnull);
            m_local.m_isnull[m_c_global->m_targetFuncNodes[i].resno - 1] = func_isnull;
        }
        ELOG_FIELD_NAME_END;
    }

    if (m_c_global->m_targetParamNum > 0) {
        for (int i = 0; i < m_c_global->m_targetParamNum; i++) {
            m_local.m_values[m_global->m_paramLoc[i].scanKeyIndx] =
                params->params[m_global->m_paramLoc[i].paramId - 1].value;
            m_local.m_isnull[m_global->m_paramLoc[i].scanKeyIndx] =
                params->params[m_global->m_paramLoc[i].paramId - 1].isnull;
        }
    }
}

bool NvmInsertFusion::execute(long max_rows, char *completionTag)
{
    bool success = false;
    errno_t errorno = EOK;
    unsigned long porcessed = 1;

    GetParameter();

    int rc = NvmSqlByPassInsert(m_global->m_reloid, m_global->m_tupDesc, m_local.m_values, m_local.m_isnull);
    if (rc == 0) {
        success = true;
        errorno =
            snprintf_s(completionTag, COMPLETION_TAG_BUFSIZE, COMPLETION_TAG_BUFSIZE - 1, "INSERT 0 %lu", porcessed);
        securec_check_ss(errorno, "\0", "\0");
    }

    m_local.m_isCompleted = true;

    return success;
}

NvmDeleteFusion::NvmDeleteFusion(MemoryContext context, CachedPlanSource *psrc, List *plantree_list,
                                 ParamListInfo params)
    : OpFusion(context, psrc, plantree_list)
{
    MemoryContext old_context = nullptr;
    if (!IsGlobal()) {
        old_context = MemoryContextSwitchTo(m_global->m_context);
        InitGlobals();
        MemoryContextSwitchTo(old_context);
    }
    old_context = MemoryContextSwitchTo(m_local.m_localContext);
    InitLocals(params);
    MemoryContextSwitchTo(old_context);
}

void NvmDeleteFusion::InitLocals(ParamListInfo params)
{
    m_local.m_tmpvals = nullptr;
    m_local.m_tmpisnull = nullptr;

    m_local.m_reslot = MakeSingleTupleTableSlot(m_global->m_tupDesc);
    initParams(params);

    m_local.m_receiver = nullptr;
    m_local.m_isInsideRec = true;
    m_local.m_optype = DELETE_FUSION;

    m_c_local.m_nvmFdwState = NvmGetFdwState(m_c_global.m_scan, m_global->m_reloid);
}

void NvmDeleteFusion::InitGlobals()
{
    m_global->m_reloid =
        getrelid(linitial_int((List *)linitial(m_global->m_planstmt->resultRelations)), m_global->m_planstmt->rtable);
    Relation rel = heap_open(m_global->m_reloid, AccessShareLock);
    m_global->m_natts = RelationGetDescr(rel)->natts;
    m_global->m_tupDesc = CreateTupleDescCopy(RelationGetDescr(rel));
    m_global->m_is_bucket_rel = RELATION_OWN_BUCKET(rel);
    m_global->m_table_type = RelationIsUstoreFormat(rel) ? TAM_USTORE : TAM_HEAP;
    heap_close(rel, AccessShareLock);
    ModifyTable *node = (ModifyTable *)m_global->m_planstmt->planTree;
    Plan *deletePlan = (Plan *)linitial(node->plans);
    m_c_global.m_scan = (ForeignScan *)deletePlan;
}

bool NvmDeleteFusion::execute(long max_rows, char *completionTag)
{
    bool success = false;
    unsigned long nprocess = 0;
    errno_t errorno = EOK;
    ParamListInfo param_list = m_local.m_outParams != nullptr ? m_local.m_outParams : m_local.m_params;

    int rc = NvmSqlByPassDelete(m_c_global.m_scan, m_global->m_tupDesc, m_c_local.m_nvmFdwState, param_list);
    if (rc >= 0) {
        nprocess = rc;
        success = true;
        errorno = snprintf_s(completionTag, COMPLETION_TAG_BUFSIZE, COMPLETION_TAG_BUFSIZE - 1, "DELETE %lu", nprocess);
        securec_check_ss(errorno, "\0", "\0");
    }

    m_local.m_isCompleted = true;

    return success;
}
