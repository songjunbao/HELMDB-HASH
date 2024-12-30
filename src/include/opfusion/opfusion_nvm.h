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
 * opfusion_nvmdb.h
 *        Declaration of nvmdb's operator for bypass executor.
 *
 * IDENTIFICATION
 *        src/include/opfusion/opfusion_nvmdb.h
 *
 * ---------------------------------------------------------------------------------------
 */

#ifndef SRC_INCLUDE_OPFUSION_OPFUSION_NVMDB_H_
#define SRC_INCLUDE_OPFUSION_OPFUSION_NVMDB_H_

#include "opfusion/opfusion.h"


class NvmSelectFusion : public OpFusion {
public:
    NvmSelectFusion(MemoryContext context, CachedPlanSource* psrc, List* plantree_list, ParamListInfo params);

    ~NvmSelectFusion()
    {}

    bool execute(long max_rows, char* completionTag);

    void InitLocals(ParamListInfo params);

    void InitGlobals();

private:
    void *m_nvmFdwState = nullptr;
    TupleDesc m_TupDescOrg = nullptr;
    int64 m_limitCount = -1;
    int64 m_limitOffset = -1;
    bool m_isUpdate = false;
    void *m_iter = nullptr;
    ForeignScan *m_scan = nullptr;
    unsigned long m_nprocessed = 0;
};

class NvmUpdateFusion : public OpFusion {
public:
    NvmUpdateFusion(MemoryContext context, CachedPlanSource* psrc, List* plantree_list, ParamListInfo params);

    ~NvmUpdateFusion()
    {}

    bool execute(long max_rows, char* completionTag);

    void InitLocals(ParamListInfo params);

    void InitGlobals();

    void UpdateTargetResult(TupleTableSlot *reslot, bool *colMayChange);

private:
    struct VarLoc {
        int varNo;
        int scanKeyIndex;
    };

    struct UpdateFusionGlobalVariable {
        int m_targetConstNum;
        int m_targetParamNum;
        VarLoc *m_targetVarLoc;
        int m_varNum;
        ConstLoc *m_targetConstLoc;
        ParamLoc *m_targetParamLoc;
        FuncExprInfo *m_targetFuncNodes;
        int m_targetFuncNum;
        ForeignScan *m_scan = nullptr;
    };

    UpdateFusionGlobalVariable *m_c_global;

    struct UpdateFusionLocaleVariable {
        void *m_nvmFdwState = nullptr;
        void *m_iter = nullptr;
        Datum *m_curVarValue;
        bool *m_curVarIsnull;
    };

    UpdateFusionLocaleVariable m_c_local;
};


class NvmInsertFusion : public OpFusion {
public:
    NvmInsertFusion(MemoryContext context, CachedPlanSource* psrc, List* plantree_list, ParamListInfo params);

    ~NvmInsertFusion()
    {}

    bool execute(long max_rows, char* completionTag);

    void InitLocals(ParamListInfo params);

    void InitGlobals();

    void GetParameter();

private:

    unsigned long ExecInsert(Relation rel, ResultRelInfo *resultRelInfo);

    struct InsertFusionGlobalVariable {
        FuncExprInfo *m_targetFuncNodes;
        int m_targetFuncNum;
        int m_targetParamNum;
        int m_targetConstNum;
        ConstLoc *m_targetConstLoc;
    };

    InsertFusionGlobalVariable *m_c_global;

    struct InsertFusionLocaleVariable {
        Datum *m_curVarValue;
        bool *m_curVarIsnull;
    };

    InsertFusionLocaleVariable m_c_local;
};


class NvmDeleteFusion : public OpFusion {
public:
    NvmDeleteFusion(MemoryContext context, CachedPlanSource* psrc, List* plantree_list, ParamListInfo params);

    ~NvmDeleteFusion()
    {}

    bool execute(long max_rows, char* completionTag);

    void InitLocals(ParamListInfo params);

    void InitGlobals();

private:

    struct DeleteFusionGlobalVariable {
        ForeignScan *m_scan = nullptr;
    };

    DeleteFusionGlobalVariable m_c_global;

    struct DeleteFusionLocaleVariable {
        void *m_nvmFdwState = nullptr;
        void *m_iter = nullptr;
    };

    DeleteFusionLocaleVariable m_c_local;
};


#endif /* SRC_INCLUDE_OPFUSION_OPFUSION_NVMDB_H_ */

