#include "nvm_init.h"
#include "nvm_access.h"
#include "nvmdb_thread.h"
#include "heap/nvm_heap.h"
#include "nvm_fdw_iter.h"
#include "opfusion/opfusion_nvm.h"

// PG fdw库的 log 和 google log 冲突
#undef LOG

#include "access/reloptions.h"
#include "commands/copy.h"
#include "commands/explain.h"
#include "commands/vacuum.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#include "utils/partitionkey.h"
#include "optimizer/var.h"
#include "catalog/pg_operator.h"
#include "parser/parsetree.h"
#include "access/sysattr.h"
#include "storage/ipc.h"

// 重新定义 google log
#undef LOG
#undef INFO
#undef FATAL
#define LOG(severity) COMPACT_GOOGLE_LOG_ ## severity.stream()

#include <map>

namespace NVMDB {
DECLARE_int64(cache_size);
DECLARE_int64(cache_elasticity);
}

void InitNvmThread();

namespace NVMDB_FDW {
union FloatConvT {
    float mV;
    uint32_t mR;
    uint8_t mC[4];
};

union DoubleConvT {
    double mV;
    uint64_t mR;
    uint16_t mC[4];
};

struct NvmRowId {
    NVMDB::RowId m_rowId{};
    uint16 m_reserve = 0x1234;  // set by memcpy
} __attribute__((packed));

class NvmTableMap {
public:
    auto Find(Oid oid) {
        return m_map.find(oid);
    }

    auto Insert(const std::pair<Oid, NVMDB::Table *> &el) {
        el.second->RefCountInc();
        return m_map.insert(el);
    }

    auto Erase(std::map<Oid, NVMDB::Table *>::iterator iter) {
        iter->second->RefCountDec();
        return m_map.erase(iter);
    }

    void Clear() {
        auto iter = m_map.begin();
        while (iter != m_map.end()) {
            iter = this->Erase(iter);
        }
    }

    auto Begin() {
        return m_map.begin();
    }

    auto End() {
        return m_map.end();
    }

private:
    std::map<Oid, NVMDB::Table *> m_map;
};

// a thread local buffer for g_nvmdbTable,
// if it cannot find in g_nvmdbTableLocal, search g_nvmdbTable with mutex
thread_local NvmTableMap g_nvmdbTableLocal;
NvmTableMap g_nvmdbTable;
static std::mutex g_tableMutex;

static const char *g_nvmdbErrcodeStr[] = {
    "success",
    "input para error",
    "unsupported col type",
    "no memory",
    "table not found",
    "index not found",
    "col not found",
    "index type not support",
    "index not support nullable col",
    "index size over limit",
    "col size over limit",
    "index not support expr"
};

static inline const char *NvmGetErrcodeStr(NVM_ERRCODE err) {
    if (likely(err < NVM_ERRCODE::NVM_ERRCODE_INVALID)) {
        return g_nvmdbErrcodeStr[static_cast<int>(err)];
    }
    CHECK(false);
    return nullptr;
}

static inline bool NvmIsTxInAbortState(NVMDB::Transaction *Tx) {
    return Tx->GetTxStatus() == NVMDB::TxStatus::WAIT_ABORT;
}

static inline void NvmRaiseAbortTxError() {
    ereport(ERROR, (errmodule(MOD_NVM), errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
                    errmsg("current transaction is aborted, commands ignored until end of transaction block, firstChar[%c]", u_sess->proc_cxt.firstChar),
                    errdetail("Please perform rollback")));
}

static NVMDB::ColumnType GetNVMType(Oid &typoid) {
    NVMDB::ColumnType type;
    switch (typoid) {
        case CHAROID:
            type = NVMDB::COL_TYPE_CHAR;
            break;
        case INT1OID:
        case BOOLOID:
            type = NVMDB::COL_TYPE_TINY;
            break;
        case INT2OID:
            type = NVMDB::COL_TYPE_SHORT;
            break;
        case INT4OID:
            type = NVMDB::COL_TYPE_INT;
            break;
        case INT8OID:
            type = NVMDB::COL_TYPE_LONG;
            break;
        case DATEOID:
            type = NVMDB::COL_TYPE_DATE;
            break;
        case TIMEOID:
            type = NVMDB::COL_TYPE_TIME;
            break;
        case TIMESTAMPOID:
            type = NVMDB::COL_TYPE_TIMESTAMP;
            break;
        case TIMESTAMPTZOID:
            type = NVMDB::COL_TYPE_TIMESTAMPTZ;
            break;
        case INTERVALOID:
            type = NVMDB::COL_TYPE_INTERVAL;
            break;
        case TINTERVALOID:
            type = NVMDB::COL_TYPE_TINTERVAL;
            break;
        case TIMETZOID:
            type = NVMDB::COL_TYPE_TIMETZ;
            break;
        case FLOAT4OID:
            type = NVMDB::COL_TYPE_FLOAT;
            break;
        case FLOAT8OID:
            type = NVMDB::COL_TYPE_DOUBLE;
            break;
        case NUMERICOID:
            type = NVMDB::COL_TYPE_DECIMAL;
            break;
        case VARCHAROID:
        case BPCHAROID:
        case TEXTOID:
            type = NVMDB::COL_TYPE_VARCHAR;
            break;
        default:
            type = NVMDB::COL_TYPE_INVALID;
    }
    return type;
}

void NvmThreadlocalTableMapClear() {
    g_nvmdbTableLocal.Clear();
}

void NvmDropTable(Oid oid) {
    auto iter = g_nvmdbTableLocal.Find(oid);
    if (iter != g_nvmdbTableLocal.End()) {
        g_nvmdbTableLocal.Erase(iter);
    }

    std::lock_guard<std::mutex> lock_guard(g_tableMutex);
    iter = g_nvmdbTable.Find(oid);
    if (iter != g_nvmdbTable.End()) {
        iter->second->Dropped();
        g_nvmdbTable.Erase(iter);
    }

    NVMDB::g_heapSpace->DropTable(oid);
}

NVMDB::Table *NvmGetTableByOid(Oid oid) {
    // 检查Table定义是否在本地缓存中
    auto iter = g_nvmdbTableLocal.Find(oid);
    if (iter != g_nvmdbTableLocal.End()) {
        if (unlikely(iter->second->IsDropped())) {
            g_nvmdbTableLocal.Erase(iter);
            return nullptr;
        }
        return iter->second;
    }

    // 检查Table定义是否在全局已经初始化
    std::lock_guard<std::mutex> lock_guard(g_tableMutex);
    iter = g_nvmdbTable.Find(oid);
    if (iter != g_nvmdbTable.End()) {
        g_nvmdbTableLocal.Insert(std::make_pair(oid, iter->second));
        return iter->second;
    }

    // Rebuild the table desc.
    uint32 tableSegHead = NVMDB::g_heapSpace->SearchTable(oid);
    if (tableSegHead == 0) {
        return nullptr;
    }
    Relation rel = RelationIdGetRelation(oid);
    NVMDB::TableDesc tableDesc;
    if (!TableDescInit(&tableDesc, rel->rd_att->natts)) {
        return nullptr;
    }
    for (int n = 0; n < rel->rd_att->natts; ++n) {
        tableDesc.col_desc[n].m_colType = GetNVMType(rel->rd_att->attrs[n].atttypid);
        tableDesc.col_desc[n].m_colOid = rel->rd_att->attrs[n].atttypid;
        if (rel->rd_att->attrs[n].attlen == -1) {
            if (tableDesc.col_desc[n].m_colOid == VARCHAROID || tableDesc.col_desc[n].m_colOid == BPCHAROID) {
                tableDesc.col_desc[n].m_colLen = rel->rd_att->attrs[n].atttypmod;
            } else if (tableDesc.col_desc[n].m_colOid == NUMERICOID) {
                tableDesc.col_desc[n].m_colLen = DECIMAL_MAX_SIZE;
            } else if (tableDesc.col_desc[n].m_colOid == TEXTOID) {
                tableDesc.col_desc[n].m_colLen = MAX_VARCHAR_LEN;
            } else {
                CHECK(false);
            }
        } else {
            tableDesc.col_desc[n].m_colLen = rel->rd_att->attrs[n].attlen;
        }
        tableDesc.col_desc[n].m_colOffset = tableDesc.row_len;
        tableDesc.col_desc[n].m_isNotNull = rel->rd_att->attrs[n].attnotnull;
        char *name = rel->rd_att->attrs[n].attname.data;
        auto rc = memcpy_s(tableDesc.col_desc[n].m_colName, sizeof(name), name, sizeof(name));
        SecureRetCheck(rc);
        tableDesc.row_len += tableDesc.col_desc[n].m_colLen;
    }
    auto *table = new (std::nothrow) NVMDB::Table(oid, tableDesc);
    table->Mount(tableSegHead);

    // Rebuild the index desc.
    ListCell *l;
    const auto *tableColDesc = table->GetColDesc();
    if (rel->rd_indexvalid == 0) {
        List *indexes = RelationGetIndexList(rel);
    }
    foreach (l, rel->rd_indexlist) {
        NVMDB::uint64 index_len = 0;
        Oid indexOid = lfirst_oid(l);
        auto *index = new (std::nothrow) NVMDB::NVMIndex(indexOid);
        Relation indexRel = RelationIdGetRelation(indexOid);
        uint32 colCount = indexRel->rd_index->indnatts;
        auto *indexDesc = NVMDB::IndexDescCreate(colCount);
        if (unlikely(false == index->SetNumTableFields(colCount))) {
            return nullptr;
        }
        for (uint32 i = 0; i < colCount; i++) {
            // in opengauss, colid start from 1 but nvmdb start from 0.
            indexDesc[i].m_colId = indexRel->rd_index->indkey.values[i] - 1;
            index->FillBitmap(indexDesc[i].m_colId);
        }
        RelationClose(indexRel);
        InitIndexDesc(indexDesc, tableColDesc, colCount, index_len);
        index->SetIndexDesc(indexDesc, colCount, index_len);
        table->AddIndex(index);
    }

    RelationClose(rel);
    g_nvmdbTable.Insert(std::make_pair(oid, table));
    g_nvmdbTableLocal.Insert(std::make_pair(oid, table));
    return table;
}

static inline NVMDB::Table *NvmGetTableByOidWrapper(Oid oid) {
    auto *table = NvmGetTableByOid(oid);
    if (unlikely(table == nullptr)) {
        ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("Get nvm table fail(%d)!", static_cast<int>(NVM_ERRCODE::NVM_ERRCODE_TABLE_NOT_FOUND))));
    }
    return table;
}

static inline void NVMVarLenFieldType(Form_pg_type typeDesc, Oid typoid, int32_t colLen, int16 *typeLen) {
    bool isBlob = false;
    if (typeDesc->typlen < 0) {
        *typeLen = (int16)colLen;
        switch (typeDesc->typstorage) {
            case 'p':
                break;
            case 'x':
            case 'm':
                if (typoid == NUMERICOID) {
                    *typeLen = DECIMAL_MAX_SIZE;
                    break;
                }
            case 'e':
                if (typoid == TEXTOID) {
                    colLen = MAX_VARCHAR_LEN;
                    *typeLen = (int16)colLen;
                }
                if (colLen > MAX_VARCHAR_LEN || colLen < 0) {
                    CHECK(false);
                } else {
                    isBlob = true;
                }
                break;
            default:
                break;
        }
    }
}

// 根据colDef 获取 type, typeLen 和 typoid
static NVM_ERRCODE GetTypeInfo(const ColumnDef *colDef, NVMDB::ColumnType &type, int16 &typeLen, Oid &typoid) {
    NVM_ERRCODE res = NVM_ERRCODE::NVM_SUCCESS;
    int32_t colLen;
    // given a TypeName colDef->typname, return a Type structure and typmod
    Type tup = typenameType(nullptr, colDef->typname, &colLen);
    Form_pg_type typeDesc = ((Form_pg_type)GETSTRUCT(tup));
    typoid = HeapTupleGetOid(tup);
    typeLen = typeDesc->typlen; // 记录长度(例如 int = 4)

    NVMVarLenFieldType(typeDesc, typoid, colLen, &typeLen);

    // colume NVM内部类型 如 int
    type = GetNVMType(typoid);
    if (type == NVMDB::COL_TYPE_INVALID) {
        res = NVM_ERRCODE::NVM_ERRCODE_UNSUPPORTED_COL_TYPE;
    }

    if (tup) {  // Release previously grabbed reference count on a tuple
        ReleaseSysCache(tup);
    }

    return res; // 返回成功或失败
}

static Datum GetTypeMax(Oid type) {
    int ret;
    Datum data = 0;
    float fdata;
    double ddata;

    switch (type) {
        case CHAROID:
        case INT1OID:
        case VARCHAROID:
        case BPCHAROID:
        case TEXTOID:
            data = std::numeric_limits<unsigned char>::max();
            break;
        case BOOLOID:
            data = 1;
            break;
        case INT2OID:
            data = std::numeric_limits<short>::max();
            break;
        case INT4OID:
            data = std::numeric_limits<int>::max();
            break;
        case INT8OID:
            data = std::numeric_limits<long>::max();
            break;
        case TIMEOID:
            data = std::numeric_limits<unsigned long>::max();
            break;
        case FLOAT4OID:
            fdata = std::numeric_limits<float>::max();
            ret = memcpy_s(&data, sizeof(fdata), &fdata, sizeof(fdata));
            SecureRetCheck(ret);
            break;
        case FLOAT8OID:
            ddata = std::numeric_limits<double>::max();
            ret = memcpy_s(&data, sizeof(ddata), &ddata, sizeof(ddata));
            SecureRetCheck(ret);
            break;
        case NUMERICOID:
        default:
            CHECK(false);
    }

    return data;
}

static Datum GetTypeMin(Oid type) {
    int ret;
    Datum data = 0;
    float fdata;
    double ddata;
    switch (type) {
        case CHAROID:
        case INT1OID:
        case VARCHAROID:
        case BPCHAROID:
        case TEXTOID:
            data = std::numeric_limits<unsigned char>::lowest();
            break;
        case BOOLOID:
            data = 1;
            break;
        case INT2OID:
            data = std::numeric_limits<short>::lowest();
            break;
        case INT4OID:
            data = std::numeric_limits<int>::lowest();
            break;
        case INT8OID:
            data = std::numeric_limits<long>::lowest();
            break;
        case TIMEOID:
            data = std::numeric_limits<unsigned long>::lowest();
            break;
        case FLOAT4OID:
            fdata = std::numeric_limits<float>::lowest();
            ret = memcpy_s(&data, sizeof(fdata), &fdata, sizeof(fdata));
            SecureRetCheck(ret);
            break;
        case FLOAT8OID:
            ddata = std::numeric_limits<double>::lowest();
            ret = memcpy_s(&data, sizeof(ddata), &ddata, sizeof(ddata));
            SecureRetCheck(ret);
            break;
        case NUMERICOID:
        default:
            CHECK(false);
    }

    return data;
}
// 之前已经检查了表是否存在
NVM_ERRCODE CreateTable(CreateForeignTableStmt *stmt, ::TransactionId tid) {
    NVMDB::TableDesc tableDesc;
    ListCell *cell = nullptr;   // PG var
    NVMDB::Table *table = nullptr; // NVMDB::Table
    NVM_ERRCODE ret = NVM_ERRCODE::NVM_SUCCESS;
    uint32 colIndex = 0;
    // 一张表最多有64列
    if (list_length(stmt->base.tableElts) > NVMDB::NVMDB_TUPLE_MAX_COL_COUNT) {
        ret = NVM_ERRCODE::NVM_ERRCODE_COL_COUNT_EXC_LIMIT;
        goto final;
    }

    // 静态函数, 初始化 NVMDB::TableDesc tableDesc
    if (!TableDescInit(&tableDesc, list_length(stmt->base.tableElts))) {
        ret = NVM_ERRCODE::NVM_ERRCODE_NO_MEM;
        goto final;
    }
    // 遍历column definitions (list of ColumnDef), 初始化 tableDesc 的 col_desc列表
    foreach (cell, stmt->base.tableElts) {
        auto *colDef = (ColumnDef *)lfirst(cell);

        if (colDef == nullptr || colDef->typname == nullptr) {
            ret = NVM_ERRCODE::NVM_ERRCODE_INPUT_PARA_ERROR;
            break;
        }
        // 根据colDef 获取 type, typeLen 和 typoid
        int16 typeLen = 0;
        auto *colDesc = &(tableDesc.col_desc[colIndex]);
        ret = GetTypeInfo(colDef, colDesc->m_colType, typeLen, colDesc->m_colOid);
        if (ret != NVM_ERRCODE::NVM_SUCCESS) {
            break;
        }
        colDesc->m_colLen = typeLen;    // 占用空间
        colDesc->m_colOffset = tableDesc.row_len;   // offset
        colDesc->m_isNotNull = colDef->is_not_null;
        errno_t rc = strcpy_s(colDesc->m_colName, NVMDB::NVM_MAX_COLUMN_NAME_LEN, colDef->colname); // 列名
        SecureRetCheck(rc);

        tableDesc.row_len += typeLen;
        colIndex++;
    }

    if (likely(ret == NVM_ERRCODE::NVM_SUCCESS)) {  // 使用初始化完毕的 tableDesc 和oid 初始化Table (在内存中)
        table = new (std::nothrow) NVMDB::Table(stmt->base.relation->foreignOid, tableDesc);
        if (likely(table != nullptr)) {
            g_tableMutex.lock();
            g_nvmdbTable.Insert(std::make_pair(stmt->base.relation->foreignOid, table));    // 插入 g_nvmdbTable 中(同样非持久化)
            g_tableMutex.unlock();
            g_nvmdbTableLocal.Insert(std::make_pair(stmt->base.relation->foreignOid, table));   // 本地缓存一份
            uint32 tableSegHead = table->CreateSegment();   // 在PMEM中初始化表
            NVMDB::g_heapSpace->CreateTable(stmt->base.relation->foreignOid, tableSegHead);
        } else {
            ret = NVM_ERRCODE::NVM_ERRCODE_NO_MEM;
        }
    }

final:
    if (ret != NVM_ERRCODE::NVM_SUCCESS) {
        TableDescDestroy(&tableDesc);
        ereport(ERROR, (errmodule(MOD_NVM), errcode(ERRCODE_INVALID_COLUMN_DEFINITION), errmsg("NVM create table fail:%s!", NvmGetErrcodeStr(ret))));
    }
    return ret;
}

// init per thread, thread run one Tx per time
NVMDB::Transaction *NVMGetCurrentTxContext() {
    if (!t_thrd.nvmdb_init) {
        InitNvmThread();
    }

    if (u_sess->nvm_cxt.m_nvmTx == nullptr) {
        u_sess->nvm_cxt.m_nvmTx = new NVMDB::Transaction();
        CHECK(u_sess->nvm_cxt.m_nvmTx != nullptr);
        u_sess->nvm_cxt.m_nvmTx->Begin();
    }
    return u_sess->nvm_cxt.m_nvmTx;
}

void NVMStateFree(NVMFdwState *nvmState) {
    if (nvmState != nullptr) {
        delete nvmState->mIter;

        if (nvmState->mAttrsUsed != nullptr) {
            pfree(nvmState->mAttrsUsed);
        }

        if (nvmState->mAttrsModified != nullptr) {
            pfree(nvmState->mAttrsModified);
        }

        list_free(nvmState->mConst.mParentExprList);
        list_free(nvmState->mConstPara.mParentExprList);

        pfree(nvmState);
    }
}

/* Convertors */
inline static void PGNumericToNVM(Numeric const n, DecimalSt &d) {
    int sign = NUMERIC_SIGN(n);

    d.m_hdr.m_flags = 0;
    d.m_hdr.m_flags |=
        (sign == NUMERIC_POS ? DECIMAL_POSITIVE
                             : (sign == NUMERIC_NEG ? DECIMAL_NEGATIVE : ((sign == NUMERIC_NAN) ? DECIMAL_NAN : 0)));
    d.m_hdr.m_ndigits = NUMERIC_NDIGITS(n);
    d.m_hdr.m_scale = NUMERIC_DSCALE(n);
    d.m_hdr.m_weight = NUMERIC_WEIGHT(n);
    d.m_round = 0;
    if (d.m_hdr.m_ndigits > 0) {
        errno_t rc = memcpy_s(d.m_digits, DECIMAL_MAX_SIZE - sizeof(DecimalSt), (void *)NUMERIC_DIGITS(n),
                              d.m_hdr.m_ndigits * sizeof(NumericDigit));
        SecureRetCheck(rc);
    }
}

inline static Numeric NVMNumericToPG(DecimalSt *d) {
    NumericVar v;

    v.ndigits = d->m_hdr.m_ndigits;
    v.dscale = d->m_hdr.m_scale;
    v.weight = (int)(int16_t)(d->m_hdr.m_weight);
    v.sign = (d->m_hdr.m_flags & DECIMAL_POSITIVE
                  ? NUMERIC_POS
                  : (d->m_hdr.m_flags & DECIMAL_NEGATIVE ? NUMERIC_NEG
                                                         : ((d->m_hdr.m_flags & DECIMAL_NAN) ? DECIMAL_NAN : 0)));
    v.buf = (NumericDigit *)&d->m_round;
    v.digits = (NumericDigit *)d->m_digits;

    return makeNumeric(&v);
}

void NVMColInitData(NVMDB::RAMTuple &tuple, uint16 colIndex, Datum datum, Oid atttypi) {
    switch (atttypi) {
        case BYTEAOID:
        case TEXTOID:
        case VARCHAROID:
        case CLOBOID:
        case BPCHAROID: {
            bytea *txt = DatumGetByteaP(datum);
            uint32 size = VARSIZE(txt);
            size -= sizeof(uint32);
            errno_t ret = memcpy_s(txt, sizeof(uint32), &size, sizeof(uint32));
            SecureRetCheck(ret);
            tuple.SetCol(colIndex, (char *)txt, size + sizeof(uint32));
            SET_VARSIZE(txt, size + sizeof(uint32));

            if ((char *)datum != (char *)txt) {
                pfree(txt);
            }
            break;
        }
        case NUMERICOID: {
            Numeric n = DatumGetNumeric(datum);
            char buf[DECIMAL_MAX_SIZE];
            auto *d = (DecimalSt *)buf;

            if (NUMERIC_NDIGITS(n) > DECIMAL_MAX_DIGITS) {
                ereport(ERROR, (errmodule(MOD_NVM), errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                                errmsg("Value exceeds maximum precision: %d", NVM_NUMERIC_MAX_PRECISION)));
                break;
            }
            PGNumericToNVM(n, *d);
            tuple.SetCol(colIndex, (char *)d, DECIMAL_SIZE(d));
            break;
        }
        case INTERVALOID:
        case TINTERVALOID:
        case TIMETZOID:
            tuple.SetCol(colIndex, (char *)datum);
            break;
        default:
            tuple.SetCol(colIndex, (char *)&datum);
            break;
    }
}

void NVMColUpdateData(NVMDB::RAMTuple &tuple, uint16 colIndex, Datum datum, Oid atttypi, uint64 len) {
    switch (atttypi) {
        case BYTEAOID:
        case TEXTOID:
        case VARCHAROID:
        case CLOBOID:
        case BPCHAROID: {
            bytea *txt = DatumGetByteaP(datum);
            uint32 size = VARSIZE(txt);
            size -= sizeof(uint32);
            errno_t ret = memcpy_s(txt, sizeof(uint32), &size, sizeof(uint32));
            SecureRetCheck(ret);
            tuple.UpdateColInc(colIndex, (char *)txt, size + sizeof(uint32));
            SET_VARSIZE(txt, size + sizeof(uint32));

            if ((char *)datum != (char *)txt) {
                pfree(txt);
            }
            break;
        }
        case NUMERICOID: {
            Numeric n = DatumGetNumeric(datum);
            char buf[DECIMAL_MAX_SIZE];
            auto *d = (DecimalSt *)buf;

            if (NUMERIC_NDIGITS(n) > DECIMAL_MAX_DIGITS) {
                ereport(ERROR, (errmodule(MOD_NVM), errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                                errmsg("Value exceeds maximum precision: %d", NVM_NUMERIC_MAX_PRECISION)));
                break;
            }
            PGNumericToNVM(n, *d);
            tuple.UpdateColInc(colIndex, (char *)d, DECIMAL_SIZE(d));
            break;
        }
        case INTERVALOID:
        case TINTERVALOID:
        case TIMETZOID:
            tuple.UpdateColInc(colIndex, (char *)datum, len);
            break;
        default:
            tuple.UpdateColInc(colIndex, (char *)&datum, len);
            break;
    }
}

void NVMInsertTuple2AllIndex(NVMDB::Transaction *tx, NVMDB::Table *table, NVMDB::RAMTuple *tuple, NVMDB::RowId rowId) {
    uint32 count = table->GetIndexCount();

    for (uint32 i = 0; i < count; i++) {
        auto *index = table->GetIndex(i);
        CHECK(index != nullptr);
        NVMDB::DRAMIndexTuple indexTuple(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(), index->GetRowLen());

        indexTuple.ExtractFromTuple(tuple);

        tx->IndexInsert(index, &indexTuple, rowId);
    }
}

void NVMDeleteTupleFromAllIndex(NVMDB::Transaction *tx, NVMDB::Table *table, NVMDB::RAMTuple *tuple, NVMDB::RowId rowId) {
    uint32 count = table->GetIndexCount();

    for (uint32 i = 0; i < count; i++) {
        NVMDB::NVMIndex *index = table->GetIndex(i);
        CHECK(index != nullptr);

        NVMDB::DRAMIndexTuple indexTuple(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(), index->GetRowLen());

        indexTuple.ExtractFromTuple(tuple);

        tx->IndexDelete(index, &indexTuple, rowId);
    }
}

void NVMInsertTuple2Index(NVMDB::Transaction *tx, NVMDB::Table *table, NVMDB::NVMIndex *index, NVMDB::RAMTuple *tuple, NVMDB::RowId rowId) {
    NVMDB::DRAMIndexTuple indexTuple(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(), index->GetRowLen());

    indexTuple.ExtractFromTuple(tuple);

    tx->IndexInsert(index, &indexTuple, rowId);
}

void NVMDeleteTupleFromIndex(NVMDB::Transaction *tx, NVMDB::Table *table, NVMDB::NVMIndex *index, NVMDB::RAMTuple *tuple, NVMDB::RowId rowId) {
    NVMDB::DRAMIndexTuple indexTuple(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(), index->GetRowLen());

    indexTuple.ExtractFromTuple(tuple);

    tx->IndexDelete(index, &indexTuple, rowId);
}

void NVMFillSlotByTuple(TupleTableSlot *slot, NVMDB::Table *table, NVMDB::RAMTuple *tuple) {
    TupleDesc tupdesc = slot->tts_tupleDescriptor;
    uint64 cols = table->GetColCount();

    for (uint64 i = 0; i < cols; i++) {
        slot->tts_isnull[i] = tuple->IsNull(i);
        if (slot->tts_isnull[i]) {
            continue;
        }
        switch (tupdesc->attrs[i].atttypid) {
            case VARCHAROID:
            case BPCHAROID:
            case TEXTOID:
            case CLOBOID:
            case BYTEAOID: {
                char *data = tuple->GetCol(i);
                uint32 len = *(uint32 *)data + VARHDRSZ;
                char *dst = (char *)palloc0(len);
                errno_t ret = memcpy_s(dst, len, data, len);
                SecureRetCheck(ret);
                auto *result = (bytea *)dst;
                SET_VARSIZE(result, len);
                slot->tts_values[i] = PointerGetDatum(result);
                break;
            }
            case NUMERICOID: {
                auto *d = (DecimalSt *)(tuple->GetCol(i));
                slot->tts_values[i] = NumericGetDatum(NVMNumericToPG(d));
                break;
            }
            case INTERVALOID:
            case TINTERVALOID:
            case TIMETZOID:
                slot->tts_values[i] = (Datum)(tuple->GetCol(i));
                break;
            default:
                tuple->GetCol(i, (char *)&(slot->tts_values[i]));
                break;
        }
    }
}

inline bool NVMIsNotEqualOper(OpExpr *op) {
    switch (op->opno) {
        case INT48NEOID:
        case BooleanNotEqualOperator:
        case 402:
        case INT8NEOID:
        case INT84NEOID:
        case INT4NEOID:
        case INT2NEOID:
        case 531:
        case INT24NEOID:
        case INT42NEOID:
        case 561:
        case 567:
        case 576:
        case 608:
        case 644:
        case FLOAT4NEOID:
        case 630:
        case 5514:
        case 643:
        case FLOAT8NEOID:
        case 713:
        case 812:
        case 901:
        case BPCHARNEOID:
        case 1071:
        case DATENEOID:
        case 1109:
        case 1551:
        case FLOAT48NEOID:
        case FLOAT84NEOID:
        case 1321:
        case 1331:
        case 1501:
        case 1586:
        case 1221:
        case 1202:
        case NUMERICNEOID:
        case 1785:
        case 1805:
        case INT28NEOID:
        case INT82NEOID:
        case 1956:
        case 3799:
        case TIMESTAMPNEOID:
        case 2350:
        case 2363:
        case 2376:
        case 2389:
        case 2539:
        case 2545:
        case 2973:
        case 3517:
        case 3630:
        case 3677:
        case 2989:
        case 3883:
        case 5551:
            return true;

        default:
            return false;
    }
}

inline List *NVMBitmapSerialize(List *result, uint8_t const *bitmap, int16_t len) {
    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int),
                                       static_cast<int>(FDW_LIST_TYPE::FDW_LIST_BITMAP), false, true));
    for (int i = 0; i < len; i++)
        result = lappend(result, makeConst(INT1OID, -1, InvalidOid, 1, Int8GetDatum(bitmap[i]), false, true));

    return result;
}

inline void NVMBitmapDeSerialize(uint8_t *bitmap, int16_t len, ListCell **cell) {
    if (cell != nullptr && *cell != nullptr) {
        int type = ((Const *)lfirst(*cell))->constvalue;
        if (type == static_cast<int>(FDW_LIST_TYPE::FDW_LIST_BITMAP)) {
            *cell = lnext(*cell);
            for (int i = 0; i < len; i++) {
                bitmap[i] = (uint8_t)((Const *)lfirst(*cell))->constvalue;
                *cell = lnext(*cell);
            }
        }
    }
}

NVMFdwState *NVMInitializeFdwState(void *fdwState, List **fdwExpr, uint64_t exTableID) {
    auto *state = reinterpret_cast<NVMFdwState *>(palloc0(sizeof(NVMFdwState)));
    List *values = (List *)fdwState;

    state->mAllocInScan = true;
    state->mForeignTableId = exTableID;
    state->mConst.mCost = std::numeric_limits<double>::max();
    state->mConstPara.mCost = std::numeric_limits<double>::max();
    if (list_length(values) > 0) {
        ListCell *cell = list_head(values);
        int type = ((Const *)lfirst(cell))->constvalue;
        if (type != static_cast<int>(FDW_LIST_TYPE::FDW_LIST_STATE)) {
            return state;
        }
        cell = lnext(cell);
        state->mCmdOper = (CmdType)((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);
        state->mForeignTableId = ((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);
        state->mNumAttrs = ((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);
        state->mCtidNum = ((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);
        state->mNumExpr = ((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);

        state->mConst.mIndex = (NVMDB::NVMIndex *)((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);
        state->mConst.mMatchCount = (uint32)((Const *)lfirst(cell))->constvalue;
        cell = lnext(cell);
        for (uint j = 0; j < NVM_MAX_KEY_COLUMNS; j++) {
            state->mConst.mOper[j] = (KEY_OPER)((Const *)lfirst(cell))->constvalue;
            cell = lnext(cell);
        }

        int len = BITMAP_GETLEN(state->mNumAttrs);
        state->mAttrsUsed = reinterpret_cast<uint8_t *> palloc0(len);
        state->mAttrsModified = reinterpret_cast<uint8_t *> palloc0(len);
        NVMBitmapDeSerialize(state->mAttrsUsed, len, &cell);

        if (fdwExpr != nullptr && *fdwExpr != nullptr) {
            ListCell *c = nullptr;
            int i = 0;

            state->mConst.mParentExprList = nullptr;

            foreach (c, *fdwExpr) {
                if (i < state->mNumExpr) {
                    i++;
                    continue;
                } else {
                    state->mConst.mParentExprList = lappend(state->mConst.mParentExprList, lfirst(c));
                }
            }

            *fdwExpr = list_truncate(*fdwExpr, state->mNumExpr);
        }
    }

    return state;
}

void *NVMSerializeFdwState(NVMFdwState *state) {
    List *result = nullptr;

    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), static_cast<int>(FDW_LIST_TYPE::FDW_LIST_STATE), false, true));
    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), Int32GetDatum(state->mCmdOper), false, true));
    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), Int32GetDatum(state->mForeignTableId), false, true));
    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), Int32GetDatum(state->mNumAttrs), false, true));
    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), Int32GetDatum(state->mCtidNum), false, true));
    result = lappend(result, makeConst(INT2OID, -1, InvalidOid, sizeof(short), Int16GetDatum(state->mNumExpr), false, true));
    result = lappend(result, makeConst(INT8OID, -1, InvalidOid, sizeof(long long), Int64GetDatum(state->mConst.mIndex), false, true));
    result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), Int32GetDatum(state->mConst.mMatchCount), false, true));
    for (uint j = 0; j < NVM_MAX_KEY_COLUMNS; j++) {
        result = lappend(result, makeConst(INT4OID, -1, InvalidOid, sizeof(int), UInt32GetDatum(state->mConst.mOper[j]), false, true));
    }

    int len = BITMAP_GETLEN(state->mNumAttrs);
    result = NVMBitmapSerialize(result, state->mAttrsUsed, len);

    NVMStateFree(state);
    return result;
}

inline bool NVMGetKeyOperation(OpExpr *op, KEY_OPER &oper) {
    switch (op->opno) {
        case FLOAT8EQOID:
        case FLOAT4EQOID:
        case INT2EQOID:
        case INT4EQOID:
        case INT8EQOID:
        case INT24EQOID:
        case INT42EQOID:
        case INT84EQOID:
        case INT48EQOID:
        case INT28EQOID:
        case INT82EQOID:
        case FLOAT48EQOID:
        case FLOAT84EQOID:
        case 5513:  // INT1EQ
        case BPCHAREQOID:
        case TEXTEQOID:
        case 92:    // CHAREQ
        case 2536:  // timestampVStimestamptz
        case 2542:  // timestamptzVStimestamp
        case 2347:  // dateVStimestamp
        case 2360:  // dateVStimestamptz
        case 2373:  // timestampVSdate
        case 2386:  // timestamptzVSdate
        case TIMESTAMPEQOID:
            oper = KEY_OPER::READ_KEY_EXACT;
            break;
        case FLOAT8LTOID:
        case FLOAT4LTOID:
        case INT2LTOID:
        case INT4LTOID:
        case INT8LTOID:
        case INT24LTOID:
        case INT42LTOID:
        case INT84LTOID:
        case INT48LTOID:
        case INT28LTOID:
        case INT82LTOID:
        case FLOAT48LTOID:
        case FLOAT84LTOID:
        case 5515:  // INT1LT
        case 1058:  // BPCHARLT
        case 631:   // CHARLT
        case TEXTLTOID:
        case 2534:  // timestampVStimestamptz
        case 2540:  // timestamptzVStimestamp
        case 2345:  // dateVStimestamp
        case 2358:  // dateVStimestamptz
        case 2371:  // timestampVSdate
        case 2384:  // timestamptzVSdate
        case TIMESTAMPLTOID:
            oper = KEY_OPER::READ_KEY_BEFORE;
            break;
        case FLOAT8LEOID:
        case FLOAT4LEOID:
        case INT2LEOID:
        case INT4LEOID:
        case INT8LEOID:
        case INT24LEOID:
        case INT42LEOID:
        case INT84LEOID:
        case INT48LEOID:
        case INT28LEOID:
        case INT82LEOID:
        case FLOAT48LEOID:
        case FLOAT84LEOID:
        case 5516:  // INT1LE
        case 1059:  // BPCHARLE
        case 632:   // CHARLE
        case 665:   // TEXTLE
        case 2535:  // timestampVStimestamptz
        case 2541:  // timestamptzVStimestamp
        case 2346:  // dateVStimestamp
        case 2359:  // dateVStimestamptz
        case 2372:  // timestampVSdate
        case 2385:  // timestamptzVSdate
        case TIMESTAMPLEOID:
            oper = KEY_OPER::READ_KEY_OR_PREV;
            break;
        case FLOAT8GTOID:
        case FLOAT4GTOID:
        case INT2GTOID:
        case INT4GTOID:
        case INT8GTOID:
        case INT24GTOID:
        case INT42GTOID:
        case INT84GTOID:
        case INT48GTOID:
        case INT28GTOID:
        case INT82GTOID:
        case FLOAT48GTOID:
        case FLOAT84GTOID:
        case 5517:       // INT1GT
        case 1060:       // BPCHARGT
        case 633:        // CHARGT
        case TEXTGTOID:  // TEXTGT
        case 2538:       // timestampVStimestamptz
        case 2544:       // timestamptzVStimestamp
        case 2349:       // dateVStimestamp
        case 2362:       // dateVStimestamptz
        case 2375:       // timestampVSdate
        case 2388:       // timestamptzVSdate
        case TIMESTAMPGTOID:
            oper = KEY_OPER::READ_KEY_AFTER;
            break;
        case FLOAT8GEOID:
        case FLOAT4GEOID:
        case INT2GEOID:
        case INT4GEOID:
        case INT8GEOID:
        case INT24GEOID:
        case INT42GEOID:
        case INT84GEOID:
        case INT48GEOID:
        case INT28GEOID:
        case INT82GEOID:
        case FLOAT48GEOID:
        case FLOAT84GEOID:
        case 5518:  // INT1GE
        case 1061:  // BPCHARGE
        case 634:   // CHARGE
        case 667:   // TEXTGE
        case 2537:  // timestampVStimestamptz
        case 2543:  // timestamptzVStimestamp
        case 2348:  // dateVStimestamp
        case 2361:  // dateVStimestamptz
        case 2374:  // timestampVSdate
        case 2387:  // timestamptzVSdate
        case TIMESTAMPGEOID:
            oper = KEY_OPER::READ_KEY_OR_NEXT;
            break;
        default:
            oper = KEY_OPER::READ_INVALID;
            break;
    }

    return (oper != KEY_OPER::READ_INVALID);
}

inline void RevertKeyOperation(KEY_OPER &oper) {
    if (oper == KEY_OPER::READ_KEY_BEFORE) {
        oper = KEY_OPER::READ_KEY_AFTER;
    } else if (oper == KEY_OPER::READ_KEY_OR_PREV) {
        oper = KEY_OPER::READ_KEY_OR_NEXT;
    } else if (oper == KEY_OPER::READ_KEY_AFTER) {
        oper = KEY_OPER::READ_KEY_BEFORE;
    } else if (oper == KEY_OPER::READ_KEY_OR_NEXT) {
        oper = KEY_OPER::READ_KEY_OR_PREV;
    }
}

// 检查给定的表达式是否可以利用NVM索引进行优化。
bool NvmMatchIndexs(NVMFdwState *state, uint32 col, NvmMatchIndexArr *matchArray, Expr *expr, Expr *parent, KEY_OPER oper) {
    bool result = false;
    uint32 indexCount = state->mTable->GetIndexCount();

    for (uint32 i = 0; i < indexCount; i++) {   // 遍历该Table的所有 NVM 索引
        NVMDB::NVMIndex *index = state->mTable->GetIndex(i);
        if (index != nullptr && index->IsFieldPresent(col)) {
            if (matchArray->m_idx[i] == nullptr) {
                matchArray->m_idx[i] = (NvmMatchIndex *)palloc0(sizeof(NvmMatchIndex)); // 记录当前列在指定索引中的匹配情况
                matchArray->m_idx[i]->m_ix = index;
            }

            result |= matchArray->m_idx[i]->SetIndexColumn(col, oper, expr, parent);
        }
    }

    return result;
}

bool IsNVMExpr(RelOptInfo *baserel, NVMFdwState *state, Expr *expr, Expr **result, NvmMatchIndexArr *matchArray) {
    bool support = false;

    switch (expr->type) {
        case T_Const:
        // 数值、字符串、布尔值等任何基本数据类型, 包含了常量的实际值以及数据类型信息.
        // 在查询 SELECT * FROM users WHERE age = 30; 中，age = 30 这个条件中的 30 就是一个 T_Const 节点,它表示一个常量值 30。
        // 一个变量引用, 通常是指向一个关系中的某个列, 包含了变量的标识信息,如表号、列号等
        case T_Var:
        // 在查询 SELECT first_name, last_name FROM users WHERE id = 5; 中，first_name、last_name 和 id 都是 T_Var 节点,它们分别表示 users 表中的对应列。
        case T_Param: { // 在查询 SELECT * FROM users WHERE age > $1; 中,$1 就是一个 T_Param 节点,它表示一个查询参数。在执行这个查询时,需要提供一个具体的参数值来替换 $1。
            if (result != nullptr)
                *result = expr;
            support = true;
            break;
        }
        case T_FuncExpr: {  // 函数调用表达式。
            // SELECT upper(first_name) FROM users WHERE id = 1;
            // 在查询优化和执行过程中, PostgreSQL 会将这个 T_FuncExpr 节点展开,并根据 upper() 函数的实现来计算结果。
            auto *func = (FuncExpr *)expr;

            if (func->funcformat == COERCE_IMPLICIT_CAST || func->funcformat == COERCE_EXPLICIT_CAST) {
                support = IsNVMExpr(baserel, state, (Expr *)linitial(func->args), result, matchArray);
            } else if (list_length(func->args) == 0) {
                support = true;
            }
            break;
        }
        case T_RelabelType: {   // 类型转换操作。
            // SELECT CAST(age AS text) FROM users; 中，CAST(age AS text) 部分就会对应一个 T_RelabelType 节点。
            support = IsNVMExpr(baserel, state, ((RelabelType *)expr)->arg, result, matchArray);
            break;
        }
        // T_OpExpr 一个操作表达式
        //  操作符: 表达式使用的操作符,如+、-、*、/等。
        //  操作数: 操作表达式的操作数,可以是常量、列引用、函数调用等其他表达式。
        //  返回类型: 操作表达式的返回数据类型。
        case T_OpExpr: {
            auto *op = (OpExpr *)expr;
            auto *l = (Expr *)linitial(op->args);   // 检查左操作数
            support = IsNVMExpr(baserel, state, l, &l, matchArray);
            if (list_length(op->args) == 1) {   // 检查操作数是否只有一个
                break;
            }

            auto *r = (Expr *)lsecond(op->args);   // 检查右操作数
            support &= IsNVMExpr(baserel, state, r, &r, matchArray);
            if (result != nullptr && support) { // result 保存经过检查后的表达式节点指针
                if (IsA(l, Var) && IsA(r, Var) && ((Var *)l)->varno == ((Var *)r)->varno) {
                    support = false;    // 如果左右操作数 l 和 r 都是 Var 节点,并且引用的是同一个关系
                }
                break;
            }

            KEY_OPER oper;
            if (support && NVMGetKeyOperation(op, oper)) {
                Var *v = nullptr;
                Expr *e = nullptr;

                if (IsA(l, Var)) {
                    if (!IsA(r, Var)) {
                        v = (Var *)l;
                        e = r;
                    } else {
                        if (((Var *)l)->varno == ((Var *)r)->varno) {  // same relation
                            return false;
                        } else if (bms_is_member(((Var *)l)->varno, baserel->relids)) {
                            v = (Var *)l;
                            e = r;
                        } else {
                            v = (Var *)r;
                            e = l;
                            RevertKeyOperation(oper);
                        }
                    }
                } else if (IsA(r, Var)) {
                    v = (Var *)r;
                    e = l;
                    RevertKeyOperation(oper);
                } else {
                    support = false;
                    break;
                }

                support = NvmMatchIndexs(state, v->varattno - 1, matchArray, e, expr, oper);
            }
            break;
        }
        default: {
            support = false;
            break;
        }
    }

    return support;
}

static inline bool IsNVMCurrentIndexBetter(double sumCostOrg, uint32 colCountOrg, double sumCostCur, uint32 colCountCur) {
    if ((colCountCur > colCountOrg) || ((colCountCur == colCountOrg) && (sumCostCur < sumCostOrg))) {
        return true;
    } else {
        return false;
    }
}

static constexpr uint32 NVM_BEST_MUL_FACTOR = 1000;

// 获取最佳的索引
NVMDB::NVMIndex *NvmGetBestIndex(NVMFdwState *state, NvmMatchIndexArr *matchArray, NVMFdwConstraint *constraint) {
    uint32 indexCount = state->mTable->GetIndexCount();
    int bestIndex = -1;
    uint32 colCount = 0;
    uint32 maxColCount = 0;
    uint32 indexColCount = 0;
    double cost = 10;
    double sumCost = 0;
    double maxCost = std::numeric_limits<double>::max();

    for (int i = 0; i < indexCount; i++) {
        NvmMatchIndex *matchIndex = matchArray->m_idx[i];
        if (matchIndex != nullptr) {
            NVMDB::NVMIndex *m_ix = matchIndex->m_ix;
            const NVMDB::IndexColumnDesc *desc = m_ix->GetIndexDesc();

            if (matchIndex->m_colMatch[0] == nullptr) {
                continue;
            }

            colCount = 1;
            indexColCount = m_ix->GetColCount();

            for (int j = 1; j < indexColCount; j++) {
                if (matchIndex->m_colMatch[j] != nullptr) {
                    colCount++;
                } else {
                    break;
                }
            }

            sumCost = colCount + NVM_BEST_MUL_FACTOR * (indexColCount - colCount);
            if (IsNVMCurrentIndexBetter(maxCost, maxColCount, sumCost, colCount)) {
                maxColCount = colCount;
                bestIndex = i;
                maxCost = sumCost;
            }
        }
    }

    if (bestIndex != -1) {
        NvmMatchIndex *matchIndex = matchArray->m_idx[bestIndex];
        constraint->mIndex = matchIndex->m_ix;
        constraint->mMatchCount = maxColCount;
        constraint->mStartupCost = 0.01;
        constraint->mCost = maxCost;
        errno_t ret = memcpy_s(constraint->mOper, sizeof(KEY_OPER) * NVM_MAX_KEY_COLUMNS,
                               matchIndex->m_opers, sizeof(KEY_OPER) * NVM_MAX_KEY_COLUMNS);
        SecureRetCheck(ret);
        list_free(constraint->mParentExprList);
        list_free(constraint->mExprList);
        constraint->mParentExprList = nullptr;
        constraint->mExprList = nullptr;
        for (int j = 0; j < maxColCount; j++) {
            constraint->mParentExprList = lappend(constraint->mParentExprList, matchIndex->m_parentColMatch[j]);
            constraint->mExprList = lappend(constraint->mExprList, matchIndex->m_colMatch[j]);
        }
    }

    return constraint->mIndex;
}

void NVMVarcharToIndexKey(Datum datum, NVMDB::DRAMIndexTuple *tuple, uint32 colIndex, uint64 maxLen) {
    bool noValue = false;

    bytea *txt = DatumGetByteaP(datum);
    NVMDB::uint64 size = VARSIZE(txt);
    char *src = VARDATA(txt);

    size -= VARHDRSZ;
    CHECK(size < maxLen);

    tuple->SetCol(colIndex, src, size);

    if ((char *)datum != (char *)txt) {
        pfree(txt);
    }
}

void NVMIndexTupleWriteData(Oid colType, NVMDB::DRAMIndexTuple *tuple, NVMFdwState *festate, uint32 colIndex, Datum datum) {
    NVMDB::NVMIndex *index = festate->mConst.mIndex;
    const NVMDB::IndexColumnDesc *desc = index->GetIndexDesc();
    CHECK(colIndex < index->GetColCount());
    uint32 len = desc[colIndex].m_colLen;

    switch (colType) {
        case BYTEAOID:
        case TEXTOID:
        case VARCHAROID:
        case CLOBOID:
        case BPCHAROID: {
            char buffer[len];
            *(uint32 *)buffer = len - sizeof(uint32);
            errno_t ret = memset_s(buffer + sizeof(uint32), len - sizeof(uint32), datum, len - sizeof(uint32));
            SecureRetCheck(ret);
            tuple->SetCol(colIndex, buffer, true);
            break;
        }
        default:
            tuple->SetCol(colIndex, (char *)&datum);
            break;
    }
}

void NVMIndexTupleFillMax(Oid colType, NVMDB::DRAMIndexTuple *tuple, NVMFdwState *festate, uint32 colIndex) {
    Datum datum = GetTypeMax(colType);
    NVMIndexTupleWriteData(colType, tuple, festate, colIndex, datum);
}

void NVMIndexTupleFillMin(Oid colType, NVMDB::DRAMIndexTuple *tuple, NVMFdwState *festate, uint32 colIndex) {
    Datum datum = GetTypeMin(colType);
    NVMIndexTupleWriteData(colType, tuple, festate, colIndex, datum);
}

void NVMFillExactValue2IndexTuple(Oid datumType, Datum datum, Oid colType, NVMDB::DRAMIndexTuple *tuple, NVMFdwState *festate, uint32 colIndex) {
    NVMDB::NVMIndex *index = festate->mConst.mIndex;
    const NVMDB::IndexColumnDesc *desc = index->GetIndexDesc();
    CHECK(colIndex < index->GetColCount());
    uint32 len = desc[colIndex].m_colLen;

    switch (colType) {
        case BYTEAOID:
        case TEXTOID:
        case VARCHAROID:
        case CLOBOID:
        case BPCHAROID: {
            bytea *txt = DatumGetByteaP(datum);
            NVMDB::uint64 size = VARSIZE(txt);
            *(uint32 *)txt = size - sizeof(uint32);
            tuple->SetCol(colIndex, (char *)txt, size);
            SET_VARSIZE(txt, size);
            if ((char *)datum != (char *)txt) {
                pfree(txt);
            }
            break;
        }
        case FLOAT4OID:
            if (datumType == FLOAT8OID) {
                DoubleConvT dc{};
                FloatConvT fc{};
                dc.mR = static_cast<uint64>(datum);
                fc.mV = static_cast<float>(dc.mV);
                auto u = static_cast<uint64>(fc.mR);
                tuple->SetCol(colIndex, reinterpret_cast<char *>(&u));
            } else {
                tuple->SetCol(colIndex, reinterpret_cast<char *>(&datum));
            }
            break;
        default:
            tuple->SetCol(colIndex, reinterpret_cast<char *>(&datum));
            break;
    }
}

void NVMFillExactValue2IndexTuples(Oid datumType, Datum datum, Oid colType,
                                   NVMDB::DRAMIndexTuple *begin,
                                   NVMDB::DRAMIndexTuple *end,
                                   NVMFdwState *festate,
                                   uint32 colIndex) {
    NVMDB::NVMIndex *index = festate->mConst.mIndex;
    const NVMDB::IndexColumnDesc *desc = index->GetIndexDesc();
    CHECK(colIndex < index->GetColCount());
    uint32 len = desc[colIndex].m_colLen;
    KEY_OPER oper = festate->mConst.mOper[colIndex];

    switch (oper) {
        case KEY_OPER::READ_KEY_EXACT:
            NVMFillExactValue2IndexTuple(datumType, datum, colType, begin, festate, colIndex);
            end->Copy(begin, colIndex);
            break;
        case KEY_OPER::READ_KEY_OR_NEXT:
        case KEY_OPER::READ_KEY_AFTER:
            NVMFillExactValue2IndexTuple(datumType, datum, colType, begin, festate, colIndex);
            NVMIndexTupleFillMax(colType, end, festate, colIndex);
            break;
        case KEY_OPER::READ_KEY_OR_PREV:
        case KEY_OPER::READ_KEY_BEFORE:
            NVMFillExactValue2IndexTuple(datumType, datum, colType, end, festate, colIndex);
            NVMIndexTupleFillMin(colType, begin, festate, colIndex);
            break;
        default:
            CHECK(false);
    }
}

NVMDB::NVMIndexIter *NvmIndexIterOpen(ForeignScanState *node, NVMFdwState *festate) {
    auto* fscan = (ForeignScan *)node->ss.ps.plan;
    auto* execExprs = (List *)ExecInitExpr((Expr *)fscan->fdw_exprs, (PlanState *)node);
    CHECK(festate->mConst.mMatchCount == list_length(execExprs));

    auto* table = festate->mTable;
    auto* index = festate->mConst.mIndex;
    NVMDB::DRAMIndexTuple indexTupleBegin(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(), index->GetRowLen());
    NVMDB::DRAMIndexTuple indexTupleEnd(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(), index->GetRowLen());

    const auto *indexDesc = index->GetIndexDesc();
    for (int i = 0; i < index->GetColCount(); i++) {
        const Relation& rel = node->ss.ss_currentRelation;
        const TupleDesc& desc = rel->rd_att;
        const Oid& colType = desc->attrs[indexDesc[i].m_colId].atttypid;
        if (i < festate->mConst.mMatchCount) {
            auto *expr = (ExprState *)list_nth(execExprs, i);
            auto* econtext = node->ss.ps.ps_ExprContext;
            bool isNull = false;
            Datum val = ExecEvalExpr((ExprState *)(expr), econtext, &isNull, nullptr);
            CHECK(!isNull);
            NVMFillExactValue2IndexTuples(expr->resultType, val, colType, &indexTupleBegin, &indexTupleEnd, festate, i);
        } else {
            NVMIndexTupleFillMin(colType, &indexTupleBegin, festate, i);
            NVMIndexTupleFillMax(colType, &indexTupleEnd, festate, i);
        }
    }

    auto ss = festate->mCurrTx->GetIndexLookupSnapshot();
    auto* result = index->GenerateIter(&indexTupleBegin, &indexTupleEnd, ss, 0, false);
    CHECK(result != nullptr);
    return result;
}

NvmFdwIter *NvmGetIter(ForeignScanState *node, NVMFdwState *festate) {
    if (festate->mIter == nullptr) {
        if (festate->mConst.mIndex != nullptr) {
            festate->mIter = new (std::nothrow) NvmFdwIndexIter(NvmIndexIterOpen(node, festate));
        } else {
            DLOG(INFO) << "Table with oid: " << festate->mForeignTableId << " does not have index!";
            festate->mIter = new (std::nothrow) NvmFdwSeqIter(NVMDB::HeapUpperRowId(festate->mTable));
        }
        CHECK(festate->mIter != nullptr);
    }

    return festate->mIter;
}

void NVMIndexRestore(NVMDB::Table *table, NVMDB::NVMIndex *index) {
    NVMDB::Transaction *tx = NVMGetCurrentTxContext();
    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());

    for (NVMDB::RowId rowId = 0; rowId < HeapUpperRowId(table); rowId++) {
        NVMDB::HamStatus status = HeapRead(tx, table, rowId, &tuple);
        if (status == NVMDB::HamStatus::OK) {
            NVMInsertTuple2Index(tx, table, index, &tuple, rowId);
        }
    }
}

void NVMIndexDeleteAllData(NVMDB::Table *table, NVMDB::NVMIndex *index) {
    NVMDB::Transaction *tx = NVMGetCurrentTxContext();
    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());

    for (NVMDB::RowId rowId = 0; rowId < HeapUpperRowId(table); rowId++) {
        NVMDB::HamStatus status = HeapRead(tx, table, rowId, &tuple);
        if (status == NVMDB::HamStatus::OK) {
            NVMDeleteTupleFromIndex(tx, table, index, &tuple, rowId);
        }
    }
}

NVM_ERRCODE CreateIndex(IndexStmt *stmt, ::TransactionId tid) {
    NVMDB::IndexColumnDesc *indexDesc;
    NVMDB::NVMIndex *index = nullptr;
    ListCell *lc = nullptr;
    NVM_ERRCODE result = NVM_ERRCODE::NVM_SUCCESS;
    uint32 i = 0;
    uint32 colCount = list_length(stmt->indexParams);
    NVMDB::uint64 index_len = 0;

    do {
        NVMDB::Table *table = NvmGetTableByOid(stmt->relation->foreignOid);
        if (unlikely(table == nullptr)) {
            result = NVM_ERRCODE::NVM_ERRCODE_TABLE_NOT_FOUND;
            break;
        }

        index = new (std::nothrow) NVMDB::NVMIndex(stmt->indexOid);
        if (unlikely(index == nullptr)) {
            result = NVM_ERRCODE::NVM_ERRCODE_NO_MEM;
            break;
        }

        indexDesc = NVMDB::IndexDescCreate(colCount);
        if (unlikely(indexDesc == nullptr)) {
            result = NVM_ERRCODE::NVM_ERRCODE_NO_MEM;
            break;
        }

        if (unlikely(!index->SetNumTableFields(table->GetColCount()))) {
            result = NVM_ERRCODE::NVM_ERRCODE_NO_MEM;
            break;
        }

        foreach (lc, stmt->indexParams) {
            auto *ielem = (IndexElem *)lfirst(lc);

            if (ielem->expr != nullptr) {
                result = NVM_ERRCODE::NVM_ERRCODE_INDEX_NOT_SUPPORT_EXPR;
                goto CREATE_INDEX_OUT;
            }

            uint32 colid = table->GetColIdByName((ielem->name != nullptr ? ielem->name : ielem->indexcolname));
            if (colid == NVMDB::InvalidColId) {
                result = NVM_ERRCODE::NVM_ERRCODE_COL_NOT_FOUND;
                goto CREATE_INDEX_OUT;
            }

            if (!IsIndexTypeSupported(table->GetColDesc(colid)->m_colType)) {
                result = NVM_ERRCODE::NVM_ERRCODE_INDEX_TYPE_NOT_SUPPORT;
                goto CREATE_INDEX_OUT;
            }

            if (!table->ColIsNotNull(colid)) {
                result = NVM_ERRCODE::NVM_ERRCODE_INDEX_NOT_SUPPORT_NULL;
                goto CREATE_INDEX_OUT;
            }

            indexDesc[i].m_colId = colid;

            index->FillBitmap(colid);

            i++;
        }

        DCHECK(i == colCount);

        const auto *tabledesc = table->GetColDesc();

        InitIndexDesc(indexDesc, tabledesc, colCount, index_len);

        if (index_len > NVMDB::KEY_DATA_LENGTH) {
            result = NVM_ERRCODE::NVM_ERRCODE_INDEX_SIZE_EXC_LIMIT;
            goto CREATE_INDEX_OUT;
        }

        index->SetIndexDesc(indexDesc, colCount, index_len);

        table->AddIndex(index);

        NVMIndexRestore(table, index);
    } while (false);

CREATE_INDEX_OUT:
    if (result != NVM_ERRCODE::NVM_SUCCESS) {
        delete index;
        IndexDescDelete(indexDesc);
        ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("NVM create index fail:%s!", NvmGetErrcodeStr(result))));
    }

    return result;
}

NVM_ERRCODE DropIndex(DropForeignStmt *stmt, ::TransactionId tid) {
    NVM_ERRCODE result = NVM_ERRCODE::NVM_SUCCESS;

    do {
        auto *table = NvmGetTableByOid(stmt->reloid);
        if (table == nullptr) {
            result = NVM_ERRCODE::NVM_ERRCODE_TABLE_NOT_FOUND;
            break;
        }

        auto *index = table->DelIndex(stmt->indexoid);
        if (index == nullptr) {
            result = NVM_ERRCODE::NVM_ERRCODE_INDEX_NOT_FOUND;
            break;
        }

        NVMIndexDeleteAllData(table, index);

        delete index;
    } while (false);

    if (result != NVM_ERRCODE::NVM_SUCCESS) {
        ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("NVM delete index fail(%d)!", result)));
    }

    return result;
}

NVM_ERRCODE DropTable(DropForeignStmt *stmt, ::TransactionId tid) {
    NVM_ERRCODE result = NVM_ERRCODE::NVM_SUCCESS;

    NvmDropTable(stmt->reloid);

    return result;
}

}  // namespace NVMDB_FDW

// ----------------------------------

Datum nvm_fdw_validator(PG_FUNCTION_ARGS) {
    /*
    * Now apply the core COPY code's validation logic for more checks.
     */
    ProcessCopyOptions(nullptr, true, nullptr);

    PG_RETURN_VOID();
}

// https://www.postgresql.org/docs/9.4/fdw-callbacks.html
// 允许 FDW 为可写外部表添加额外的目标列。这些目标列用于在执行 UPDATE 和 DELETE 操作时提供 FDW 识别和更新/删除特定行的必要信息。
// 当执行 UPDATE 或 DELETE 操作时，FDW 需要知道如何识别要更新或删除的特定行。对于可写外部表，FDW 可能需要额外的信息，例如行 ID 或主键列的值。
// AddForeignUpdateTargets 函数允许 FDW 将这些额外的信息作为隐藏的或“垃圾”目标列添加到从外部表检索的列列表中。
// 这些目标列必须标记为 resjunk = true，并具有在执行时可以标识它们的唯一 resname。
// 在重写器中调用，而不是在规划器中, parsetree 是 UPDATE 或 DELETE 命令的解析树，而 target_rte 和 target_relation 描述了目标外部表。
static void NVMAddForeignUpdateTargets(Query *parsetree, RangeTblEntry *targetRte, Relation targetRelation) {
    DLOG(INFO) << "NVMAddForeignUpdateTargets is called!";
    /* Make a Var representing the desired value */
    Var *var = makeVar(parsetree->resultRelation, SelfItemPointerAttributeNumber, TIDOID, -1, InvalidOid, 0);

    /* Wrap it in a resjunk TLE with the right name ... */
    const char *attrname = NVMDB_FDW::NVM_REC_TID_NAME; // // 添加一个名为 "ctid" 的隐藏目标列

    TargetEntry *tle = makeTargetEntry((Expr *)var, list_length(parsetree->targetList) + 1, pstrdup(attrname), true);

    /* ... and add it to the query's targetlist */
    parsetree->targetList = lappend(parsetree->targetList, tle);
}

// GetForeignRelSize 函数用于获取外部表的关系大小估计。它在规划涉及外部表的查询的开始时调用。
// return the column size of the table (maybe for indexing)
// "Path" functions are called before modify the table
// GetForeignRelSize 函数接收以下参数：
// root: 规划器有关查询的全局信息
// baserel: 规划器有关此外部表的信息
// foreigntableid: 外部表的 pg_class OID
// 该函数应更新 baserel->rows 以表示表扫描返回的预期行数，同时考虑限制限定符完成的过滤。baserel->rows的初始值只是一个常量默认估计，应该替换它。
// 该函数还可以选择更新 baserel->width，如果它可以计算结果行平均宽度的更好估计。
// root 和 baserel 中的信息可用于减少必须从外部表获取的信息量（从而降低成本）。
// baserel->baserestrictinfo包含限制限定符（WHERE 子句），这些限定符应用于过滤要获取的行。
// （FDW 本身不需要强制执行这些限定符，因为核心执行器可以改为检查它们。）
// baserel->reltargetlist 可用于确定需要获取哪些列；
// 但请注意，它仅列出必须由 ForeignScan 计划节点发出的列，而不是用于限定符评估但未由查询输出的列。
static void NVMGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid) {
    DLOG(INFO) << "NVMGetForeignRelSize is called!";
    auto *festate = reinterpret_cast<NVMDB_FDW::NVMFdwState *>(palloc0(sizeof(NVMDB_FDW::NVMFdwState)));
    festate->mConst.mCost = std::numeric_limits<double>::max();
    festate->mConstPara.mCost = std::numeric_limits<double>::max();
    festate->mCurrTx = NVMDB_FDW::NVMGetCurrentTxContext();
    ForeignTable *ftable = GetForeignTable(foreigntableid);
    Relation rel = RelationIdGetRelation(ftable->relid);
    auto *table = NVMDB_FDW::NvmGetTableByOidWrapper(RelationGetRelid(rel));
    festate->mTable = table;
    festate->mRowIndex = 0;
    festate->mNumAttrs = RelationGetNumberOfAttributes(rel);
    int len = BITMAP_GETLEN(festate->mNumAttrs);
    festate->mAttrsUsed = reinterpret_cast<uint8_t *>(palloc0(len));
    festate->mAttrsModified = reinterpret_cast<uint8_t *>(palloc0(len));
    bool needWholeRow = rel->trigdesc && rel->trigdesc->trig_insert_after_row;
    TupleDesc desc = RelationGetDescr(rel);

    if (NVMDB_FDW::NvmIsTxInAbortState(festate->mCurrTx)) {
        NVMDB_FDW::NvmRaiseAbortTxError();
    }

    Bitmapset *attrs = nullptr;
    ListCell *lc = nullptr;
    foreach (lc, baserel->baserestrictinfo) {
        auto *ri = (RestrictInfo *)lfirst(lc);

        if (!needWholeRow) {
            pull_varattnos((Node *)ri->clause, baserel->relid, &attrs);
        }
    }

    if (needWholeRow) {
        for (int i = 0; i < desc->natts; i++) {
            if (!desc->attrs[i].attisdropped) {
                BITMAP_SET(festate->mAttrsUsed, (desc->attrs[i].attnum - 1));
            }
        }
    } else {
        /* Pull "var" clauses to build an appropriate target list */
        pull_varattnos((Node *)baserel->reltarget->exprs, baserel->relid, &attrs);
        if (attrs != nullptr) {
            bool all = bms_is_member(-FirstLowInvalidHeapAttributeNumber, attrs);
            for (int i = 0; i < festate->mNumAttrs; i++) {
                if (all || bms_is_member(desc->attrs[i].attnum - FirstLowInvalidHeapAttributeNumber, attrs)) {
                    BITMAP_SET(festate->mAttrsUsed, (desc->attrs[i].attnum - 1));
                }
            }
        }
    }

    baserel->fdw_private = festate;
    static constexpr double NVMDB_ESTIMATED_QUANTITY = 100000;
    static constexpr double NVMDB_START_UP_COST = 0.1;
    baserel->rows = NVMDB_ESTIMATED_QUANTITY;
    baserel->tuples = NVMDB_ESTIMATED_QUANTITY;
    festate->mConst.mStartupCost = NVMDB_START_UP_COST;
    festate->mConst.mCost = baserel->rows * festate->mConst.mStartupCost;
    festate->mConstPara.mStartupCost = NVMDB_START_UP_COST;
    festate->mConstPara.mCost = baserel->rows * festate->mConst.mStartupCost;

    RelationClose(rel);
}

// GetForeignPaths 用于为外部表扫描创建可能的访问路径。它在查询规划期间调用。
// foreigntableid: the corresponding table id
// root: the returned data is stored in root
// GetForeignPaths 函数接收以下参数：
//     root: 规划器有关查询的全局信息
//     baserel: 规划器有关此外部表的信息
//     foreigntableid: 外部表的 pg_class OID
// 该函数必须为外部表扫描生成至少一个访问路径（ForeignPath 节点），并必须调用 add_path 将每个此类路径添加到 baserel->pathlist。
// 建议使用 create_foreignscan_path 来构建 ForeignPath 节点。
// 该函数可以生成多个访问路径，例如，具有有效路径键以表示预排序结果的路径。
// 每个访问路径都必须包含成本估计，并且可以包含任何 FDW 私有信息，这些信息对于标识预期的特定扫描方法是必需的。
// 1. 将私有信息存储在 ForeignPath 节点的 fdw_private 字段
// 2. fdw_private 被声明为一个 List 指针，但实际上可以包含任何内容，因为核心规划器不会触及它。
// 3. 之后调用GetForeignPlan
static void NVMGetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid) {
    DLOG(INFO) << "NVMGetForeignPaths is called!";

    NVMDB_FDW::NvmMatchIndexArr matchArray;
    auto *pFdwState = (NVMDB_FDW::NVMFdwState *)baserel->fdw_private;  // plan state
    ListCell *lc = nullptr;
    foreach (lc, baserel->baserestrictinfo) {   // base restrict info包含限制限定符（WHERE 子句），这些限定符应用于过滤要获取的行。
        auto *ri = (RestrictInfo *)lfirst(lc);

        NVMDB_FDW::IsNVMExpr(baserel, pFdwState, ri->clause, nullptr, &matchArray);

        pFdwState->mLocalConds = lappend(pFdwState->mLocalConds, ri->clause);
    }

    List *bestClause = nullptr;
    if (NVMDB_FDW::NvmGetBestIndex(pFdwState, &matchArray, &(pFdwState->mConst)) != nullptr) {
        double ntuples = pFdwState->mConst.mCost;
        ntuples = ntuples * clauselist_selectivity(root, bestClause, 0, JOIN_INNER, nullptr);
        ntuples = clamp_row_est(ntuples);
        baserel->rows = baserel->tuples = ntuples;
    }

    List *usablePathkeys = nullptr;
    Path *fpReg = (Path *)create_foreignscan_path(
        root,
        baserel, pFdwState->mConst.mStartupCost, pFdwState->mConst.mCost,
        usablePathkeys, nullptr, /* no outer rel either */
        nullptr,                 // private data will be assigned later
        nullptr);

    bool hasRegularPath = false;
    foreach (lc, baserel->pathlist) {
        Path *path = (Path *)lfirst(lc);
        if (IsA(path, IndexPath) && path->param_info == nullptr) {
            hasRegularPath = true;
            break;
        }
    }
    if (!hasRegularPath) {
        add_path(root, baserel, fpReg);
    }
    set_cheapest(baserel);

    Path *bestPath = nullptr;
    if (!IS_PGXC_COORDINATOR && list_length(baserel->cheapest_parameterized_paths) > 0) {
        foreach (lc, baserel->cheapest_parameterized_paths) {
            bestPath = (Path *)lfirst(lc);
            if (IsA(bestPath, IndexPath) && bestPath->param_info) {
                auto *ip = (IndexPath *)bestPath;
                bestClause = ip->indexclauses;
                break;
            }
        }
        usablePathkeys = nullptr;
    }

    Path *fpIx = nullptr;
    if (bestClause != nullptr) {
        matchArray.Clear();

        foreach (lc, bestClause) {
            auto *ri = (RestrictInfo *)lfirst(lc);
            NVMDB_FDW::IsNVMExpr(baserel, pFdwState, ri->clause, nullptr, &matchArray);
        }

        if (NVMDB_FDW::NvmGetBestIndex(pFdwState, &matchArray, &(pFdwState->mConstPara)) != nullptr) {
            double ntuples = pFdwState->mConstPara.mCost;
            ntuples = ntuples * clauselist_selectivity(root, bestClause, 0, JOIN_INNER, nullptr);
            ntuples = clamp_row_est(ntuples);
            baserel->rows = baserel->tuples = ntuples;
            fpIx = (Path *)create_foreignscan_path(root, baserel, pFdwState->mConstPara.mStartupCost,
                                                   pFdwState->mConstPara.mCost, usablePathkeys,
                                                   nullptr,  /* no outer rel either */
                                                   nullptr,  // private data will be assigned later
                                                   nullptr);

            fpIx->param_info = bestPath->param_info;
        }
    }

    List *newPath = nullptr;
    List *origPath = baserel->pathlist;
    // disable index path
    foreach (lc, baserel->pathlist) {
        Path *path = (Path *)lfirst(lc);
        if (IsA(path, ForeignPath))
            newPath = lappend(newPath, path);
        else
            pfree(path);
    }

    list_free(origPath);
    baserel->pathlist = newPath;
    if (hasRegularPath)
        add_path(root, baserel, fpReg);
    if (fpIx != nullptr)
        add_path(root, baserel, fpIx);
    set_cheapest(baserel);
}

// GetForeignPlan 函数用于从选定的外部访问路径创建 ForeignScan 计划节点。它在查询规划结束时调用。
// GetForeignPlan 函数接收以下参数：
//     root: 规划器有关查询的全局信息
//     baserel: 规划器有关此外部表的信息
//     foreigntableid: 外部表的 pg_class OID
//     foreignpath: 由 GetForeignPaths 选择的 ForeignPath
//     targetlist: 计划节点要发出的目标列表
//     scan_clauses: 计划节点要强制执行的限制子句
// 该函数必须创建并返回一个 ForeignScan 计划节点；建议使用 make_foreignscan 来构建 ForeignScan 节点。
// GetForeignPlan 还必须处理 scan_clauses 列表中的限制子句。它可以将这些子句添加到 ForeignScan 计划节点的 qual 列表中，以便在执行时由执行器检查。
// FDW 还可以使用 scan_clauses 列表中的信息来生成 fdw_exprs 和 fdw_private 列表，这些列表将在执行时由 ForeignScan 计划节点使用。
static ForeignScan *NVMGetForeignPlan(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid,
                                      ForeignPath *best_path, List *tlist, List *scan_clauses, Plan *outer_plan) {
    DLOG(INFO) << "NVMGetForeignPlan is called!";
    bool sqlBypass = true;
    NVMDB_FDW::NVMFdwState *pFdwState = (NVMDB_FDW::NVMFdwState *)baserel->fdw_private;  // plan state
    if (best_path->path.param_info && pFdwState->mConstPara.mIndex != nullptr) {
        if (pFdwState->mConst.mIndex != nullptr) {
            list_free(pFdwState->mConst.mParentExprList);
            list_free(pFdwState->mConst.mExprList);
        }

        int ret = memcpy_s(&(pFdwState->mConst), sizeof(NVMDB_FDW::NVMFdwConstraint),
                           &(pFdwState->mConstPara), sizeof(NVMDB_FDW::NVMFdwConstraint));
        SecureRetCheck(ret);
        pFdwState->mConstPara.mIndex = nullptr;
        pFdwState->mConstPara.mMatchCount = 0;
        pFdwState->mConstPara.mParentExprList = nullptr;
        pFdwState->mConstPara.mExprList = nullptr;

        // 参数索引不走sqlByPass
        sqlBypass = false;
    }

    List *remote = nullptr;
    if (pFdwState->mConst.mIndex != nullptr) {
        pFdwState->mNumExpr = list_length(pFdwState->mConst.mExprList);
        remote = list_concat(pFdwState->mConst.mExprList, pFdwState->mConst.mParentExprList);
    } else {
        // 顺序查找不走SqlByPass
        sqlBypass = false;
        pFdwState->mNumExpr = 0;
    }

    ListCell *lc = nullptr;
    foreach (lc, scan_clauses) {
        auto *ri = (RestrictInfo *)lfirst(lc);

        // add OR conditions which where not handled by previous functions
        if (ri->orclause != nullptr)
            pFdwState->mLocalConds = lappend(pFdwState->mLocalConds, ri->clause);
        else if (IsA(ri->clause, BoolExpr)) {
            auto *e = (BoolExpr *)ri->clause;
            if (e->boolop == NOT_EXPR) {
                pFdwState->mLocalConds = lappend(pFdwState->mLocalConds, ri->clause);
            }
        } else if (IsA(ri->clause, OpExpr)) {
            auto *e = (OpExpr *)ri->clause;
            if (NVMDB_FDW::NVMIsNotEqualOper(e) || !list_member(remote, e)) {
                pFdwState->mLocalConds = lappend(pFdwState->mLocalConds, ri->clause);
            }
        }
    }
    baserel->fdw_private = nullptr;
    List *quals = pFdwState->mLocalConds;
    uint32 count = 0;
    if (sqlBypass) {
        // 全量约束存储在quals，索引相关的约束存放在mParentExprList，只有两个一致，且类型为T_Const或T_Param，才走sqlByPass
        if (list_length(quals) == list_length(pFdwState->mConst.mParentExprList) && list_is_subset(pFdwState->mConst.mParentExprList, quals)) {
            foreach(lc, remote) {
                Expr *expr = (Expr *)lfirst(lc);
                if (expr->type != T_Const && expr->type != T_Param) {
                    sqlBypass = false;
                    break;
                }
                count++;
                if (count == pFdwState->mNumExpr) {
                    break;
                }
            }
        } else {
            sqlBypass = false;
        }
    }

    if (pFdwState->mConst.mParentExprList) {
        pfree(pFdwState->mConst.mParentExprList);
        pFdwState->mConst.mParentExprList = nullptr;
    }
    
    ::Index scanRelid = baserel->relid;
    ForeignScan *scan = make_foreignscan(
        tlist,
        quals,
        scanRelid,
        remote, /* no expressions to evaluate */
        (List *)NVMDB_FDW::NVMSerializeFdwState(pFdwState),
        nullptr,
        nullptr,
        nullptr
#if PG_VERSION_NUM >= 90500
        ,
        nullptr, nullptr, nullptr
#endif
    );

    scan->sqlByPass = sqlBypass;

    return scan;
}

// PlanForeignModify 函数用于规划对外部表的修改操作（例如，INSERT、UPDATE 和 DELETE）。它在查询规划期间调用。
// call first before NVMBeginForeignModify
// return fdwState metadata in PlannerInfo
// PlanForeignModify 函数接收以下参数：
//     root: 规划器有关查询的全局信息
//     baserel: 规划器有关目标外部表的信息
//     foreigntableid: 外部表的 pg_class OID
//     operation: 要执行的修改操作（INSERT、UPDATE 或 DELETE）
//     targetlist: 修改操作的目标列列表
//     scan_clauses: 修改操作的扫描子句（仅适用于 UPDATE 和 DELETE）
//     update_clauses: 修改操作的更新子句（仅适用于 UPDATE）
// 该函数应返回一个包含一个或多个计划路径的列表，这些路径表示如何执行修改操作。
// 如果修改操作可以在外部服务器上完全执行，则 FDW 可以生成表示该操作的路径并将其插入 UPPERREL_FINAL 上层关系中。
// UPPERREL_FINAL 中插入的路径负责实现查询的所有行为。
List *NVMPlanForeignModify(PlannerInfo *root, ModifyTable *plan, ::Index resultRelation, int subplanIndex) {
    DLOG(INFO) << "NVMPlanForeignModify is called!";
    NVMDB::Transaction *currTx = NVMDB_FDW::NVMGetCurrentTxContext();
    if (NVMDB_FDW::NvmIsTxInAbortState(currTx)) {
        NVMDB_FDW::NvmRaiseAbortTxError();
    }
    RangeTblEntry *rte = planner_rt_fetch(resultRelation, root);
    Relation rel = heap_open(rte->relid, NoLock);
    TupleDesc desc = RelationGetDescr(rel);
    uint8_t attrsModify[BITMAP_GETLEN(desc->natts)];
    uint8_t *ptrAttrsModify = attrsModify;

    NVMDB_FDW::NVMFdwState *fdwState = nullptr;
    if ((int)resultRelation < root->simple_rel_array_size && root->simple_rel_array[resultRelation] != nullptr) {
        if (root->simple_rel_array[resultRelation]->fdw_private != nullptr) {
            fdwState = (NVMDB_FDW::NVMFdwState *)root->simple_rel_array[resultRelation]->fdw_private;
            ptrAttrsModify = fdwState->mAttrsUsed;
        }
    } else {
        fdwState = reinterpret_cast<NVMDB_FDW::NVMFdwState *>(palloc0(sizeof(NVMDB_FDW::NVMFdwState)));
        fdwState->mConst.mCost = std::numeric_limits<double>::max();
        fdwState->mConstPara.mCost = std::numeric_limits<double>::max();
        fdwState->mCmdOper = plan->operation;
        fdwState->mForeignTableId = rte->relid;
        fdwState->mNumAttrs = RelationGetNumberOfAttributes(rel);

        auto *table = NVMDB_FDW::NvmGetTableByOidWrapper(RelationGetRelid(rel));
        fdwState->mTable = table;

        int len = BITMAP_GETLEN(fdwState->mNumAttrs);
        fdwState->mAttrsUsed = reinterpret_cast<uint8_t *>(palloc0(len));
        fdwState->mAttrsModified = reinterpret_cast<uint8_t *>(palloc0(len));
    }

    switch (plan->operation) {
        case CMD_INSERT: {
            for (int i = 0; i < desc->natts; i++) {
                if (!desc->attrs[i].attisdropped) {
                    BITMAP_SET(fdwState->mAttrsUsed, (desc->attrs[i].attnum - 1));
                }
            }
            break;
        }
        case CMD_UPDATE: {
            errno_t rc = memset_s(attrsModify, BITMAP_GETLEN(desc->natts), 0, BITMAP_GETLEN(desc->natts));
            SecureRetCheck(rc);
            for (int i = 0; i < desc->natts; i++) {
                if (bms_is_member(desc->attrs[i].attnum - FirstLowInvalidHeapAttributeNumber, rte->updatedCols)) {
                    BITMAP_SET(ptrAttrsModify, (desc->attrs[i].attnum - 1));
                }
            }
            break;
        }
        case CMD_DELETE: {
            if (list_length(plan->returningLists) > 0) {
                errno_t rc = memset_s(attrsModify, BITMAP_GETLEN(desc->natts), 0, BITMAP_GETLEN(desc->natts));
                SecureRetCheck(rc);
                for (int i = 0; i < desc->natts; i++) {
                    if (!desc->attrs[i].attisdropped) {
                        BITMAP_SET(ptrAttrsModify, (desc->attrs[i].attnum - 1));
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    heap_close(rel, NoLock);
    return ((fdwState == nullptr) ? (List *)NVMDB_FDW::NVMBitmapSerialize(nullptr, attrsModify, BITMAP_GETLEN(desc->natts))
                                  : (List *)NVMDB_FDW::NVMSerializeFdwState(fdwState));
}

// ExplainForeignScan 函数用于解释 ForeignScan 计划节点。它在查询执行期间调用。
// ExplainForeignScan 函数接收以下参数：
// node: 要解释的 ForeignScan 计划节点
// context: 解释上下文
static void NVMExplainForeignScan(ForeignScanState *node, ExplainState *es) {
    DLOG(INFO) << "NVMExplainForeignScan is called!";

    bool isLocal = false;
    NVMDB_FDW::NVMFdwState *festate = nullptr;
    if (node->fdw_state != nullptr) {
        festate = (NVMDB_FDW::NVMFdwState *)node->fdw_state;
    } else {
        auto *fscan = (ForeignScan *)node->ss.ps.plan;
        festate = NVMDB_FDW::NVMInitializeFdwState(fscan->fdw_private, &fscan->fdw_exprs,
                                                   RelationGetRelid(node->ss.ss_currentRelation));
        isLocal = true;
    }

    if (festate->mConst.mIndex != nullptr) {
        // details for index
        appendStringInfoSpaces(es->str, es->indent);
        Node *qual = (Node *)make_ands_explicit(festate->mConst.mParentExprList);

        /* Set up deparsing context */
        List *context = deparse_context_for_planstate((Node *)&(node->ss.ps), nullptr, es->rtable);

        /* Deparse the expression */
        char *exprstr = deparse_expression(qual, context, true, false);

        if (t_thrd.explain_cxt.explain_perf_mode != EXPLAIN_NORMAL && es->planinfo->m_detailInfo) {
            es->planinfo->m_detailInfo->set_plan_name<true, true>();
            appendStringInfo(es->planinfo->m_detailInfo->info_str, "%s: %s\n", "Index Cond", exprstr);
        }

        /* And add to es->str */
        ExplainPropertyText("Index Cond", exprstr, es);
        static constexpr int NVMDB_CURRENT_INDENT_LEVEL = 2;
        es->indent += NVMDB_CURRENT_INDENT_LEVEL;
    }

    if (isLocal) {
        NVMDB_FDW::NVMStateFree(festate);
    }
}

// BeginForeignScan 函数用于初始化外部表扫描。它在查询执行期间调用。
// BeginForeignScan 函数接收以下参数：
// node: 要执行的 ForeignScan 计划节点
// scanstate: 扫描状态
static void NVMBeginForeignScan(ForeignScanState *node, int eflags) {
    DLOG(INFO) << "NVMBeginForeignScan is called!";

    node->ss.is_scan_end = false;
    Oid tableId = RelationGetRelid(node->ss.ss_currentRelation);
    NVMDB::Table *table = NVMDB_FDW::NvmGetTableByOidWrapper(tableId);
    auto *fscan = (ForeignScan *)node->ss.ps.plan;
    auto *festate = NVMDB_FDW::NVMInitializeFdwState(fscan->fdw_private, &fscan->fdw_exprs, tableId);
    festate->mCurrTx = NVMDB_FDW::NVMGetCurrentTxContext();
    festate->mTable = table;
    festate->mRowIndex = 0;
    node->fdw_state = festate;

    if (NVMDB_FDW::NvmIsTxInAbortState(festate->mCurrTx)) {
        NVMDB_FDW::NvmRaiseAbortTxError();
    }

    if (node->ss.ps.state->es_result_relation_info &&
        (RelationGetRelid(node->ss.ps.state->es_result_relation_info->ri_RelationDesc) ==
         RelationGetRelid(node->ss.ss_currentRelation)))
        node->ss.ps.state->es_result_relation_info->ri_FdwState = festate;

    ListCell *t = nullptr;
    foreach (t, node->ss.ps.plan->targetlist) {
        auto *tle = (TargetEntry *)lfirst(t);
        Var *v = (Var *)tle->expr;
        if (v->varattno == SelfItemPointerAttributeNumber && v->vartype == TIDOID) {
            festate->mCtidNum = tle->resno;
            break;
        }
    }
}

// NVMIterateForeignScan 函数用于从外部表扫描中获取下一行。它在查询执行期间调用。
// NVMIterateForeignScan 函数接收以下参数：
// node: 要执行的 ForeignScan 计划节点
// scanstate: 扫描状态
// cursor: 游标
//    //  使用游标从外部数据源读取下一行
//    ...
//    // 将下一行存储在扫描状态中
//    scanstate->cursortuple = tuple;
//    // 返回扫描状态
//    return scanstate;
static TupleTableSlot *NVMIterateForeignScan(ForeignScanState *node) {
    DLOG(INFO) << "NVMIterateForeignScan is called!";
    if (node->ss.is_scan_end) {
        return nullptr;
    }

    auto *festate = (NVMDB_FDW::NVMFdwState *)node->fdw_state;
    TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
    NVMDB::Table *table = festate->mTable;
    NVMDB::HamStatus status;
    bool found = false;
    TupleTableSlot *result = nullptr;

    char *tupleAddr = (char *)palloc(table->GetRowLen());
    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen(), tupleAddr);

    auto *iter = NVMDB_FDW::NvmGetIter(node, festate);
    while (iter->Valid()) {
        status = NVMDB::HeapRead(festate->mCurrTx, table, iter->GetRowId(), &tuple);
        if (status == NVMDB::HamStatus::OK) {
            found = true;
            break;
        }
        iter->Next();
    }

    if (found) {
        (void)ExecClearTuple(slot);
        NVMDB_FDW::NVMFillSlotByTuple(slot, table, &tuple);
        ExecStoreVirtualTuple(slot);
        result = slot;

        if (festate->mCtidNum > 0) {  // update or delete
            NVMDB_FDW::NvmRowId rowId;
            rowId.m_rowId = iter->GetRowId();
            HeapTuple resultTup = ExecFetchSlotTuple(slot);
            errno_t ret = memcpy_s(&resultTup->t_self, sizeof(rowId), &rowId, sizeof(rowId));
            SecureRetCheck(ret);
            HeapTupleSetXmin(resultTup, InvalidTransactionId);
            HeapTupleSetXmax(resultTup, InvalidTransactionId);
            HeapTupleHeaderSetCmin(resultTup->t_data, InvalidTransactionId);
        }
        iter->Next();
    } else {
        pfree(tupleAddr);
    }

    if (!iter->Valid()) {
        node->ss.is_scan_end = true;
    }

    return result;
}

// ReScanForeignScan 函数用于重新扫描外部表。它在查询执行期间调用。
// ReScanForeignScan 函数接收以下参数：
//     node: 要执行的 ForeignScan 计划节点
//     scanstate: 扫描状态
// 关闭游标
// 重新获取游标
// 重置扫描状态
static void NVMReScanForeignScan(ForeignScanState *node) {
    DLOG(INFO) << "NVMReScanForeignScan is called!";
    auto *festate = (NVMDB_FDW::NVMFdwState *)node->fdw_state;
    node->ss.is_scan_end = false;

    if (NVMDB_FDW::NvmIsTxInAbortState(festate->mCurrTx)) {
        NVMDB_FDW::NvmRaiseAbortTxError();
    }

    if (festate->mIter != nullptr) {
        delete festate->mIter;
        festate->mIter = nullptr;
    }

    NVMDB_FDW::NvmGetIter(node, festate);
}

// EndForeignScan 函数用于结束外部表扫描。它在查询执行期间调用。
// EndForeignScan 函数接收以下参数：
// node: 要执行的 ForeignScan 计划节点
// scanstate: 扫描状态
// 清理步骤可能包括关闭游标和释放任何其他资源。
static void NVMEndForeignScan(ForeignScanState *node) {
    DLOG(INFO) << "NVMEndForeignScan is called!";
    auto *fdwState = (NVMDB_FDW::NVMFdwState *)node->fdw_state;
    if (fdwState->mAllocInScan) {
        NVMDB_FDW::NVMStateFree(fdwState);
        node->fdw_state = nullptr;
    }
}

static bool NVMAnalyzeForeignTable(Relation relation, AcquireSampleRowsFunc *func, BlockNumber *totalpages,
                                   void *additionalData, bool estimateTableRowNum) {
    DLOG(INFO) << "NVMAnalyzeForeignTable is empty!";
    return true;
}

static int NVMAcquireSampleRowsFunc(Relation relation, int elevel, HeapTuple *rows, int targrows, double *totalrows,
                                    double *totaldeadrows, void *additionalData, bool estimateTableRowNum) {
    DLOG(INFO) << "NVMAcquireSampleRowsFunc is empty!";
    return 0;
}

// ValidateTableDef 函数用于验证外部表的表定义。它在创建或更改外部表时调用。
// ValidateTableDef 函数接收以下参数：
//     node: 要验证的外部表
//     relation: 外部表的 pg_class 记录
//     options: 创建或更改外部表的选项
// 表定义的验证可能涉及检查列类型、约束和任何其他相关信息。
static void NVMValidateTableDef(Node *obj) {
    DLOG(INFO) << "NVMValidateTableDef is called!";
    // get tid (or sub tid) from src/gausskernel/storage/access/transam/xact.cpp
    // TransactionId is thread local
    ::TransactionId tid = GetCurrentTransactionId();
    if (obj == nullptr) {
        return;
    }

    switch (nodeTag(obj)) {
        case T_CreateForeignTableStmt: {
            // create FOREIGN table nvm_test(x int not null, y float, z VARCHAR(100)) server nvm_server;
            // call when creating a table, CreateForeignTableStmt is in src/include/nodes/parsenodes.h
            NVMDB_FDW::CreateTable((CreateForeignTableStmt *)obj, tid);
            break;
        }
        case T_IndexStmt: {
            NVMDB_FDW::CreateIndex((IndexStmt *)obj, tid);
            break;
        }
        case T_DropForeignStmt: {
            auto *stmt = (DropForeignStmt *)obj;
            switch (stmt->relkind) {
                case RELKIND_INDEX:
                    NVMDB_FDW::DropIndex(stmt, tid);
                    break;
                case RELKIND_RELATION:
                    NVMDB_FDW::DropTable(stmt, tid);
                    break;
                default:
                    ereport(ERROR, (errcode(ERRCODE_FDW_OPERATION_NOT_SUPPORTED), errmodule(MOD_NVM),
                                    errmsg("The operation is not supported on nvmdb right now.")));
                    break;
            }
            break;
        }
        default:
            ereport(ERROR, (errcode(ERRCODE_FDW_OPERATION_NOT_SUPPORTED), errmodule(MOD_NVM),
                            errmsg("The operation is not supported on nvmdb right now.")));
    }
}

// BeginForeignModify 函数用于初始化外部表的修改操作（例如，INSERT、UPDATE 和 DELETE）。它在查询执行期间调用。
// BeginForeignModify 函数接收以下参数：
//     mtstate: 正在执行的 ModifyTable 计划节点的整体状态
//     rinfo: 描述目标外部表的 ResultRelInfo 结构
//     fdw_private: 由 PlanForeignModify 生成的私有数据（如果有）
//     subplan_index: 标识这是 ModifyTable 计划节点的哪个目标
//     eflags: 描述执行器为此计划节点的操作模式的标志位
// FDW 可以使用 mtstate 和 rinfo 中的信息来执行规划操作。
// 规划操作可能涉及生成将附加到执行阶段执行更新操作的 ModifyTable 计划节点的 FDW 私有信息。
// 1. 生成将附加到 ModifyTable 计划节点的 FDW 私有信息
// 2. 将 FDW 私有信息存储在 mtstate 中
static void NVMBeginForeignModify(ModifyTableState *mtstate, ResultRelInfo *resultRelInfo, List *fdwPrivate, int subplanIndex, int eflags) {
    DLOG(INFO) << "NVMBeginForeignModify is called!";
    NVMDB_FDW::NVMFdwState *festate = nullptr;

    if (fdwPrivate != nullptr && resultRelInfo->ri_FdwState == nullptr) {
        festate = NVMDB_FDW::NVMInitializeFdwState(fdwPrivate, nullptr, RelationGetRelid(resultRelInfo->ri_RelationDesc));
        festate->mAllocInScan = false;
        festate->mCurrTx = NVMDB_FDW::NVMGetCurrentTxContext();
        festate->mTable = NVMDB_FDW::NvmGetTableByOidWrapper(RelationGetRelid(resultRelInfo->ri_RelationDesc));
        resultRelInfo->ri_FdwState = festate;
    } else {
        festate = (NVMDB_FDW::NVMFdwState *)resultRelInfo->ri_FdwState;
        int len = BITMAP_GETLEN(festate->mNumAttrs);
        if (fdwPrivate != nullptr) {
            ListCell *cell = list_head(fdwPrivate);
            NVMDB_FDW::NVMBitmapDeSerialize(festate->mAttrsModified, len, &cell);

            for (int i = 0; i < festate->mNumAttrs; i++) {
                if (BITMAP_GET(festate->mAttrsModified, i)) {
                    BITMAP_SET(festate->mAttrsUsed, i);
                }
            }
        } else {
            errno_t rc = memset_s(festate->mAttrsUsed, len, 0xff, len);
            SecureRetCheck(rc);
            rc = memset_s(festate->mAttrsModified, len, 0xff, len);
            SecureRetCheck(rc);
        }
    }

    if (NVMDB_FDW::NvmIsTxInAbortState(festate->mCurrTx)) {
        NVMDB_FDW::NvmRaiseAbortTxError();
    }

    // Update FDW operation
    festate->mCtidNum = ExecFindJunkAttributeInTlist(mtstate->mt_plans[subplanIndex]->plan->targetlist, NVMDB_FDW::NVM_REC_TID_NAME);
    festate->mCmdOper = mtstate->operation;
}

// ExecForeignInsert 函数用于将单个元组插入到外部表中。它在执行器启动期间调用。
//     estate: 查询的全局执行状态
//     rinfo: 描述目标外部表的 ResultRelInfo 结构
//     slot: 包含要插入的元组的元组表槽
//     planSlot: 包含由 ModifyTable 计划节点的子计划生成的元组的元组表槽
// 如果插入成功，则返回包含实际插入数据的槽
static TupleTableSlot *NVMExecForeignInsert(EState *estate, ResultRelInfo *resultRelInfo, TupleTableSlot *slot, TupleTableSlot *planSlot) {
    DLOG(INFO) << "NVMExecForeignInsert is called!";
    auto *tx = NVMDB_FDW::NVMGetCurrentTxContext();
    auto *nvmState = (NVMDB_FDW::NVMFdwState *)resultRelInfo->ri_FdwState;
    TupleDesc tupdesc = slot->tts_tupleDescriptor;

    if (NVMDB_FDW::NvmIsTxInAbortState(tx)) {
        NVMDB_FDW::NvmRaiseAbortTxError();
    }

    auto* table = NVMDB_FDW::NvmGetTableByOidWrapper(RelationGetRelid(resultRelInfo->ri_RelationDesc));
    if (nvmState == nullptr) {
        nvmState = reinterpret_cast<NVMDB_FDW::NVMFdwState *>(palloc0(sizeof(NVMDB_FDW::NVMFdwState)));
        nvmState->mCurrTx = tx;
        nvmState->mTable = table;
        nvmState->mNumAttrs = RelationGetNumberOfAttributes(resultRelInfo->ri_RelationDesc);
        nvmState->mConst.mCost = std::numeric_limits<double>::max();
        nvmState->mConstPara.mCost = std::numeric_limits<double>::max();
        resultRelInfo->ri_FdwState = nvmState;
    }

    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());

    for (uint64_t i = 0, j = 1; i < nvmState->mNumAttrs; i++, j++) {
        bool isnull = false;
        // heap_slot_getattr: the insert value (for an attribute, not a line) from pg.heap
        Datum value = heap_slot_getattr(slot, j, &isnull);

        if (!isnull) {
            NVMDB_FDW::NVMColInitData(tuple, i, value, tupdesc->attrs[i].atttypid);
            tuple.SetNull(i, false);
        } else {
            tuple.SetNull(i, true);
        }
    }

    // the data has not been insert into NVM for now
    auto rowId = HeapInsert(tx, table, &tuple);

    NVMDB_FDW::NVMInsertTuple2AllIndex(tx, table, &tuple, rowId);

    return slot;
}

// ExecForeignUpdate 函数用于更新外部表中的单个元组。它在执行器启动期间调用。
//     estate: 查询的全局执行状态
//     rinfo: 描述目标外部表的 ResultRelInfo 结构
//     slot: 包含要更新的元组的新数据的元组表槽
//     planSlot: 包含由 ModifyTable 计划节点的子计划生成的元组的元组表槽
// 使用游标从外部数据源读取元组
// 更新元组
// 使用游标向外部数据源写入更新后的元组
// 如果更新成功，则返回包含实际更新的行数据的槽
static TupleTableSlot *NVMExecForeignUpdate(EState *estate, ResultRelInfo *resultRelInfo, TupleTableSlot *slot, TupleTableSlot *planSlot) {
    DLOG(INFO) << "NVMExecForeignUpdate is called!";

    auto *fdwState = (NVMDB_FDW::NVMFdwState *)resultRelInfo->ri_FdwState;
    auto num = static_cast<AttrNumber>(fdwState->mCtidNum - 1);
    NVMDB::Table *table = fdwState->mTable;

    DCHECK(num == table->GetColCount());
    if (fdwState->mCtidNum == 0 || planSlot->tts_nvalid < fdwState->mCtidNum || planSlot->tts_isnull[num]) {
        CHECK(false);
    }

    NVMDB_FDW::NvmRowId rowId;
    int ret = memcpy_s(&rowId, sizeof(rowId), reinterpret_cast<ItemPointerData *>(planSlot->tts_values[num]), sizeof(rowId));
    SecureRetCheck(ret);

    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());
    auto ret2 = NVMDB::HeapRead(fdwState->mCurrTx, fdwState->mTable, rowId.m_rowId, &tuple);
    if (ret2 != NVMDB::HamStatus::OK) {
        CHECK(false);
    }

    NVMDB::RAMTuple tupleOrg(table->GetColDesc(), table->GetRowLen());
    tupleOrg.CopyRow(tuple);

    const NVMDB::ColumnDesc *desc = table->GetColDesc();
    TupleDesc tupdesc = slot->tts_tupleDescriptor;
    for (uint64 i = 0, j = 1; i < num; i++, j++) {
        if (BITMAP_GET(fdwState->mAttrsModified, i)) {
            bool isnull = false;
            Datum value = heap_slot_getattr(planSlot, j, &isnull);
            if (!isnull) {
                NVMDB_FDW::NVMColUpdateData(tuple, i, value, tupdesc->attrs[i].atttypid, desc[i].m_colLen);
                tuple.SetNull(i, false);
            } else {
                tuple.SetNull(i, true);
            }
        }
    }

    auto ret3 = NVMDB::HeapUpdate(fdwState->mCurrTx, fdwState->mTable, rowId.m_rowId, &tuple);
    if (ret3 != NVMDB::HamStatus::OK) {
        ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("NVM Update fail(%d)!", static_cast<int>(ret3))));
    }

    std::vector<bool> indexColChange(table->GetIndexCount(), false);
    for (uint64 i = 0; i < num; i++) {
        if (BITMAP_GET(fdwState->mAttrsModified, i)) {
            for (uint32 k = 0; k < indexColChange.size(); k++) {
                NVMDB::NVMIndex *index = table->GetIndex(k);
                if (index != nullptr && index->IsFieldPresent(i)) {
                    indexColChange[k] = true;
                }
            }
        }
    }

    for (uint32 k = 0; k < indexColChange.size(); k++) {
        if (indexColChange[k]) {
            NVMDB::NVMIndex *index = table->GetIndex(k);
            NVMDB_FDW::NVMInsertTuple2Index(fdwState->mCurrTx, table, index, &tuple, rowId.m_rowId);
            NVMDB_FDW::NVMDeleteTupleFromIndex(fdwState->mCurrTx, table, index, &tupleOrg, rowId.m_rowId);
        }
    }

    if (resultRelInfo->ri_projectReturning) {
        return planSlot;
    }
    estate->es_processed++;
    return nullptr;
}

// ExecForeignDelete 函数用于从外部表中删除单个元组。它在执行器启动期间调用。
//     estate: 查询的全局执行状态
//     rinfo: 描述目标外部表的 ResultRelInfo 结构
//     slot: 在调用时不包含任何有用的内容，但可用于保存返回的元组
//     planSlot: 包含由 ModifyTable 计划节点的子计划生成的元组的元组表槽
// 返回槽中的数据仅在 DELETE 查询具有 RETURNING 子句或外部表具有 AFTER ROW 触发器时使用。
// 使用游标从外部数据源读取元组
// 删除元组
// 使用游标向外部数据源写入更新后的元组
// 如果删除成功，则返回包含已删除行的槽
static TupleTableSlot *NVMExecForeignDelete(EState *estate, ResultRelInfo *resultRelInfo, TupleTableSlot *slot, TupleTableSlot *planSlot) {
    DLOG(INFO) << "NVMExecForeignDelete is called!";
    auto *fdwState = (NVMDB_FDW::NVMFdwState *)resultRelInfo->ri_FdwState;
    auto num = static_cast<AttrNumber>(fdwState->mCtidNum - 1);
    if (fdwState->mCtidNum == 0 || planSlot->tts_nvalid < fdwState->mCtidNum || planSlot->tts_isnull[num]) {
        CHECK(false);
    }

    NVMDB_FDW::NvmRowId rowId;
    auto ret1 = memcpy_s(&rowId, sizeof(rowId), reinterpret_cast<ItemPointerData *>(planSlot->tts_values[num]), sizeof(rowId));
    SecureRetCheck(ret1);

    NVMDB::Table *table = fdwState->mTable;
    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());
    auto ret2 = NVMDB::HeapRead(fdwState->mCurrTx, table, rowId.m_rowId, &tuple);
    if (ret2 != NVMDB::HamStatus::OK) {
        CHECK(false);
    }

    auto ret3 = NVMDB::HeapDelete(fdwState->mCurrTx, fdwState->mTable, rowId.m_rowId);
    if (ret3 != NVMDB::HamStatus::OK) {
        ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("NVM Delete fail(%d)!", static_cast<int>(ret3))));
    }

    NVMDB_FDW::NVMDeleteTupleFromAllIndex(fdwState->mCurrTx, fdwState->mTable, &tuple, rowId.m_rowId);

    if (resultRelInfo->ri_projectReturning) {
        return planSlot;
    }
    estate->es_processed++;
    return nullptr;
}

// EndForeignModify 函数用于结束外部表的修改操作。它在执行器关闭期间调用。
static void NVMEndForeignModify(EState *estate, ResultRelInfo *resultRelInfo) {
    DLOG(INFO) << "NVMEndForeignModify is called!";
    auto *fdwState = (NVMDB_FDW::NVMFdwState *)resultRelInfo->ri_FdwState;

    if (!fdwState->mAllocInScan) {
        NVMDB_FDW::NVMStateFree(fdwState);
        resultRelInfo->ri_FdwState = nullptr;
    }
}

static int NVMIsForeignRelationUpdatable(Relation rel) {
    DLOG(INFO) << "NVMIsForeignRelationUpdatable is called!";
    return (1 << CMD_UPDATE) | (1 << CMD_INSERT) | (1 << CMD_DELETE);
}

static int NVMGetFdwType() {
    DLOG(INFO) << "NVMGetFdwType is called!";
    return NVM_ORC;
}

static void NVMTruncateForeignTable(TruncateStmt *stmt, Relation rel) {
    DLOG(INFO) << "NVMTruncateForeignTable is empty!";
}

static void NVMVacuumForeignTable(VacuumStmt *stmt, Relation rel) {
    DLOG(INFO) << "NVMVacuumForeignTable is empty!";
}

static uint64_t NVMGetForeignRelationMemSize(Oid reloid, Oid ixoid) {
    DLOG(INFO) << "NVMGetForeignRelationMemSize is empty!";
    return 0;
}

static void NVMNotifyForeignConfigChange() {
    DLOG(INFO) << "NVMNotifyForeignConfigChange is empty!";
}

static void NVMXactCallback(XactEvent event, void *arg) {
    DLOG(INFO) << "NVMXactCallback is call! event == " << event;
    NVMDB::Transaction *trans = NVMDB_FDW::NVMGetCurrentTxContext();
    if (event == XACT_EVENT_START) {
        trans->Begin();
    } else if (event == XACT_EVENT_COMMIT) {
        trans->Commit();
    } else if (event == XACT_EVENT_ABORT) {
        trans->Abort();
    }
}

static void NVMSubxactCallback(SubXactEvent event, SubTransactionId mySubid, SubTransactionId parentSubid, void *arg) {
    DLOG(INFO) << "NVMSubxactCallback is empty!";
}

Datum nvm_fdw_handler(PG_FUNCTION_ARGS) {
    FdwRoutine *fdwroutine = makeNode(FdwRoutine);
    fdwroutine->AddForeignUpdateTargets = NVMAddForeignUpdateTargets;
    fdwroutine->GetForeignRelSize = NVMGetForeignRelSize;
    fdwroutine->GetForeignPaths = NVMGetForeignPaths;
    fdwroutine->GetForeignPlan = NVMGetForeignPlan;
    fdwroutine->PlanForeignModify = NVMPlanForeignModify;
    fdwroutine->ExplainForeignScan = NVMExplainForeignScan;
    fdwroutine->BeginForeignScan = NVMBeginForeignScan;
    fdwroutine->IterateForeignScan = NVMIterateForeignScan;
    fdwroutine->ReScanForeignScan = NVMReScanForeignScan;
    fdwroutine->EndForeignScan = NVMEndForeignScan;
    fdwroutine->AnalyzeForeignTable = NVMAnalyzeForeignTable;
    fdwroutine->AcquireSampleRows = NVMAcquireSampleRowsFunc;
    fdwroutine->ValidateTableDef = NVMValidateTableDef;
    fdwroutine->PartitionTblProcess = nullptr;
    fdwroutine->BuildRuntimePredicate = nullptr;
    fdwroutine->BeginForeignModify = NVMBeginForeignModify;
    fdwroutine->ExecForeignInsert = NVMExecForeignInsert;
    fdwroutine->ExecForeignUpdate = NVMExecForeignUpdate;
    fdwroutine->ExecForeignDelete = NVMExecForeignDelete;
    fdwroutine->EndForeignModify = NVMEndForeignModify;
    fdwroutine->IsForeignRelUpdatable = NVMIsForeignRelationUpdatable;
    fdwroutine->GetFdwType = NVMGetFdwType;
    fdwroutine->TruncateForeignTable = NVMTruncateForeignTable;
    fdwroutine->VacuumForeignTable = NVMVacuumForeignTable;
    fdwroutine->GetForeignRelationMemSize = NVMGetForeignRelationMemSize;
    fdwroutine->GetForeignMemSize = nullptr;
    fdwroutine->GetForeignSessionMemSize = nullptr;
    fdwroutine->NotifyForeignConfigChange = NVMNotifyForeignConfigChange;

    NVMDB_FDW::NVMGetCurrentTxContext();

    if (!u_sess->nvm_cxt.m_initFlag) {
        RegisterXactCallback(NVMXactCallback, nullptr);
        RegisterSubXactCallback(NVMSubxactCallback, nullptr);
        u_sess->nvm_cxt.m_initFlag = true;
    }

    PG_RETURN_POINTER(fdwroutine);
}

void InitNvm() {
    int ret;
    struct stat st{};
    std::string nvmDirPath = g_instance.attr.attr_common.nvm_directory;
    std::string dataPath = g_instance.attr.attr_common.data_directory;
    std::string singlePath;
    bool needInit = false;

    if (!is_absolute_path(nvmDirPath)) {
        // for one path, default is pg_nvm
        nvmDirPath = dataPath + "/" + nvmDirPath;
        if (stat(nvmDirPath.c_str(), &st) != 0) {
            needInit = true;
        }
    } else {
        // for absolute path, set by user. Count must less than or equal to NVMDB_MAX_GROUP
        for (int i = 0, j = 0; j < nvmDirPath.size(); ++j) {
            if (nvmDirPath[j] == ';' || j == nvmDirPath.size() - 1) {
                if (j == nvmDirPath.size() - 1) {
                    j++;
                }
                DCHECK(j > i);
                singlePath = nvmDirPath.substr(i, j - i);
                if (stat(singlePath.c_str(), &st) != 0) {
                    needInit = true;
                }
                i = j + 1;
            }
        }
    }

    NVMDB::FLAGS_cache_size = gflags::Int64FromEnv("NVMCacheSize", 16384);
    NVMDB::FLAGS_cache_elasticity = gflags::Int64FromEnv("NVMCacheElasticity", 64);
    LOG(INFO) << "NVMDB lru size: " << NVMDB::FLAGS_cache_size << ", elasticity: " << NVMDB::FLAGS_cache_elasticity;

    if (needInit) {
        LOG(INFO) << "NVMDB begin init.";
        NVMDB::InitDB(nvmDirPath);
    } else {
        LOG(INFO) << "NVMDB begin bootstrap.";
        NVMDB::BootStrap(nvmDirPath);
    }
    LOG(INFO) << "From now on, NVMDB can serve requests.";
}

void UinitNvm() {
    NVMDB::ExitDBProcess();
}

void UinitNvmSession() {
    if (u_sess->nvm_cxt.m_nvmTx != nullptr) {
        delete u_sess->nvm_cxt.m_nvmTx;
        u_sess->nvm_cxt.m_nvmTx = nullptr;
    }
}

void NVMDBOnExist(int code, Datum arg) {
    DCHECK(t_thrd.nvmdb_init == true);
    UinitNvmSession();
    NVMDB::DestroyThreadLocalVariables();
    NVMDB_FDW::NvmThreadlocalTableMapClear();
    t_thrd.nvmdb_init = false;
}

void InitNvmThread() {
    if (!t_thrd.nvmdb_init) {
        NVMDB::InitThreadLocalVariables();
        on_proc_exit(NVMDBOnExist, 0);
        t_thrd.nvmdb_init = true;
    }
}

Datum NvmSqlByPassEvalSimpleArg(Node *arg, bool *is_null, ParamListInfo paramList)
{
    switch (nodeTag(arg)) {
        case T_Const:
            *is_null = ((Const *)arg)->constisnull;
            return ((Const *)arg)->constvalue;
        case T_Param:
            if (paramList->params[((Param *)arg)->paramid - 1].isnull) {
                *is_null = true;
            }
            return paramList->params[((Param *)arg)->paramid - 1].value;
        default:
            Assert(false);
            return 0;
    }
}

NVMDB::NVMIndexIter *NvmSqlByPassIndexIterOpen(ForeignScan *fscan, TupleDesc desc, NVMDB_FDW::NVMFdwState *festate,
                                               ParamListInfo paramList)
{
    NVMDB::NVMIndexIter *result = nullptr;
    NVMDB::Table *table = festate->mTable;
    ExprContext *econtext = nullptr;
    List *execExprs = fscan->fdw_exprs;
    bool is_null = false;
    NVMDB::NVMIndex *index = festate->mConst.mIndex;
    uint32 indexColCount = index->GetColCount();
    const NVMDB::IndexColumnDesc *indexDesc = index->GetIndexDesc();
    bool reverse = false;

    NVMDB::DRAMIndexTuple indexTupleBegin(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(),
                                          index->GetRowLen());
    NVMDB::DRAMIndexTuple indexTupleEnd(table->GetColDesc(), index->GetIndexDesc(), index->GetColCount(),
                                        index->GetRowLen());

    for (int i = 0; i < indexColCount; i++) {
        Oid colType = desc->attrs[indexDesc[i].m_colId].atttypid;
        if (i < festate->mConst.mMatchCount) {
            Expr *expr = (Expr *)list_nth(execExprs, i);
            Datum val = NvmSqlByPassEvalSimpleArg((Node *)expr, &is_null, paramList);
            if (likely(!is_null)) {
                NVMDB_FDW::NVMFillExactValue2IndexTuples(colType, val, colType, &indexTupleBegin, &indexTupleEnd,
                                                         festate, i);
            } else {
                Assert(false);
            }
        } else {
            NVMDB_FDW::NVMIndexTupleFillMin(colType, &indexTupleBegin, festate, i);
            NVMDB_FDW::NVMIndexTupleFillMax(colType, &indexTupleEnd, festate, i);
        }
    }

    auto ss = festate->mCurrTx->GetIndexLookupSnapshot();
    result = index->GenerateIter(&indexTupleBegin, &indexTupleEnd, ss, 0, false);
    Assert(result != nullptr);

    return result;
}

void NVMFillSlotByTupleSqlByPass(TupleTableSlot *slot, NVMDB::Table *table, NVMDB::RAMTuple *tuple, List *targetList)
{
    TupleDesc tupdesc = slot->tts_tupleDescriptor;
    uint64 cols = tupdesc->natts;
    ListCell *l = nullptr;
    uint64 index = 0;

    foreach (l, targetList) {
        TargetEntry *tle = (TargetEntry *)lfirst(l);
        if (tle->resjunk) {
            /* resjunk is true then the column is a working column 
            (such as a sort key) that should be removed from the final 
            output of the query. */
            continue;
        }

        slot->tts_isnull[index] = tuple->IsNull(index);
        if (slot->tts_isnull[index]) {
            index++;
            continue;
        }

        Form_pg_attribute attr = &tupdesc->attrs[index];
        uint32 col = tle->resorigcol - 1;
        switch (attr->atttypid) {
            case VARCHAROID:
            case BPCHAROID:
            case TEXTOID:
            case CLOBOID:
            case BYTEAOID: {
                char *data = tuple->GetCol(col);
                bytea *result = (bytea *)data;
                uint32 len = *(uint32 *)data;
                SET_VARSIZE(result, len + VARHDRSZ);
                slot->tts_values[index] = PointerGetDatum(result);
                break;
            }
            case NUMERICOID: {
                NVMDB_FDW::DecimalSt *d = (NVMDB_FDW::DecimalSt *)(tuple->GetCol(col));
                slot->tts_values[index] = NumericGetDatum(NVMDB_FDW::NVMNumericToPG(d));
                break;
            }
            case INTERVALOID:
            case TINTERVALOID:
            case TIMETZOID:
                slot->tts_values[index] = (Datum)(tuple->GetCol(col));
                break;
            default:
                tuple->GetCol(col, (char *)&(slot->tts_values[index]));
                break;
        }
        index++;
    }

    Assert(index == cols);
    return;
}

int NvmSqlByPassQuery(ForeignScan *fscan, TupleDesc desc, void *state, TupleTableSlot *slot, ParamListInfo paramList,
                      void **index)
{
    NVMDB_FDW::NVMFdwState *festate = (NVMDB_FDW::NVMFdwState *)state;
    NVMDB::NVMIndexIter *indexIter = nullptr;
    NVMDB_FDW::NvmFdwIter *iter = nullptr;
    List *targetList = fscan->scan.plan.targetlist;

    if (*index == nullptr) {
        indexIter = NvmSqlByPassIndexIterOpen(fscan, desc, festate, paramList);
        iter = new (std::nothrow) NVMDB_FDW::NvmFdwIndexIter(indexIter);
        *index = iter;
    } else {
        iter = (NVMDB_FDW::NvmFdwIter *)*index;
    }

    NVMDB::Table *table = festate->mTable;
    NVMDB::HamStatus status;
    bool found = false;

    char *tupleAddr = (char *)palloc(table->GetRowLen());
    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen(), tupleAddr);

    while (iter->Valid()) {
        status = NVMDB::HeapRead(festate->mCurrTx, table, iter->GetRowId(), &tuple);
        if (status == NVMDB::HamStatus::OK) {
            found = true;
            break;
        }
        iter->Next();
    }

    if (found) {
        ExecClearTuple(slot);
        NVMFillSlotByTupleSqlByPass(slot, table, &tuple, targetList);
        ExecStoreVirtualTuple(slot);
        iter->Next();
        return 0;
    } else {
        pfree(tupleAddr);
        return -1;
    }
}

int NvmSqlByPassUpdate(ForeignScan *fscan, TupleDesc desc, void *state, TupleTableSlot *slot, ParamListInfo paramList,
                       NvmUpdateFusion *updateFusion)
{
    NVMDB_FDW::NVMFdwState *festate = (NVMDB_FDW::NVMFdwState *)state;
    NVMDB_FDW::NvmFdwIter *iter = nullptr;
    NVMDB::Table *table = festate->mTable;
    NVMDB::HamStatus status;
    NVMDB::RowId rowId = 0;
    bool found = false;
    const NVMDB::ColumnDesc *nvmDesc = table->GetColDesc();
    uint32 indexCount = table->GetIndexCount();
    uint32 colCount = table->GetColCount();
    bool colMayChange[colCount];
    bool indexColChange[indexCount];
    int processCount = 0;
    uint64 i = 0;
    uint64 j = 0;

    iter = new (std::nothrow) NVMDB_FDW::NvmFdwIndexIter(NvmSqlByPassIndexIterOpen(fscan, desc, festate, paramList));

    while (true) {
        NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());
        found = false;

        while (iter->Valid()) {
            status = NVMDB::HeapRead(festate->mCurrTx, table, iter->GetRowId(), &tuple);
            if (status == NVMDB::HamStatus::OK) {
                found = true;
                break;
            }
            iter->Next();
        }

        if (found) {
            NVMDB::RAMTuple::ColumnUpdate updates[colCount];
            uint32 updateCount = 0;
            NVMDB::RAMTuple tupleUpdate(table->GetColDesc(), table->GetRowLen());
            rowId = iter->GetRowId();
            iter->Next();
            memset(colMayChange, 0, sizeof(colMayChange));
            memset(indexColChange, 0, sizeof(indexColChange));

            ExecClearTuple(slot);
            NVMDB_FDW::NVMFillSlotByTuple(slot, table, &tuple);
            ExecStoreVirtualTuple(slot);

            updateFusion->UpdateTargetResult(slot, colMayChange);

            for (i = 0, j = 1; i < colCount; i++, j++) {
                bool isnull = false;
                Datum value = heap_slot_getattr(slot, j, &isnull);

                if (!isnull) {
                    NVMDB_FDW::NVMColInitData(tupleUpdate, i, value, desc->attrs[i].atttypid);
                    if (colMayChange[i] && !tupleUpdate.ColEqual(i, tuple.GetCol(i))) {
                        updates[updateCount++].m_colId = i;
                    } else {
                        colMayChange[i] = false;
                    }
                } else {
                    tupleUpdate.SetNull(i, true);
                    if (!tuple.IsNull(i)) {
                        updates[updateCount++].m_colId = i;
                    }
                }
            }

            processCount++;

            if (updateCount != 0) {
                tupleUpdate.UpdateColsRecord(updates, updateCount);
            } else {
                continue;
            }

            NVMDB::HamStatus result = NVMDB::HeapUpdate(festate->mCurrTx, table, rowId, &tupleUpdate);
            if (result != NVMDB::HamStatus::OK) {
                ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("NVM Update fail(%d)!", result)));
            }

            for (int i = 0; i < colCount; i++) {
                if (colMayChange[i]) {
                    for (uint32 k = 0; k < indexCount; k++) {
                        NVMDB::NVMIndex *index = table->GetIndex(k);
                        if (index != nullptr && index->IsFieldPresent(i)) {
                            indexColChange[k] = true;
                        }
                    }
                }
            }

            for (uint32 k = 0; k < indexCount; k++) {
                if (indexColChange[k]) {
                    NVMDB::NVMIndex *index = table->GetIndex(k);
                    NVMDB_FDW::NVMInsertTuple2Index(festate->mCurrTx, table, index, &tupleUpdate, rowId);
                    NVMDB_FDW::NVMDeleteTupleFromIndex(festate->mCurrTx, table, index, &tuple, rowId);
                }
            }
        } else {
            delete iter;

            return processCount;
        }
    }
}

int NvmSqlByPassInsert(Oid tableId, TupleDesc tupDesc, Datum *value, bool *isnull)
{
    NVMDB::Table *table = nullptr;
    NVMDB::RowId rowId;
    NVMDB::Transaction *trx = NVMDB_FDW::NVMGetCurrentTxContext();

    table = NVMDB_FDW::NvmGetTableByOid(tableId);
    if (table == nullptr) {
        Assert(false);
        return -1;
    }

    NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());

    for (uint32 i = 0; i < tupDesc->natts; i++) {
        if (!isnull[i]) {
            NVMDB_FDW::NVMColInitData(tuple, i, value[i], tupDesc->attrs[i].atttypid);
        } else {
            tuple.SetNull(i, true);
        }
    }

    rowId = NVMDB::HeapInsert(trx, table, &tuple);

    NVMDB_FDW::NVMInsertTuple2AllIndex(trx, table, &tuple, rowId);

    return 0;
}

int NvmSqlByPassDelete(ForeignScan *fscan, TupleDesc desc, void *state, ParamListInfo paramList)
{
    NVMDB_FDW::NVMFdwState *festate = (NVMDB_FDW::NVMFdwState *)state;
    NVMDB_FDW::NvmFdwIter *iter = nullptr;
    NVMDB::Table *table = festate->mTable;
    NVMDB::RowId rowId = 0;
    bool found = false;
    int processCount = 0;
    NVMDB::HamStatus result;

    iter = new (std::nothrow) NVMDB_FDW::NvmFdwIndexIter(NvmSqlByPassIndexIterOpen(fscan, desc, festate, paramList));

    while (true) {
        NVMDB::RAMTuple tuple(table->GetColDesc(), table->GetRowLen());
        found = false;

        while (iter->Valid()) {
            result = NVMDB::HeapRead(festate->mCurrTx, table, iter->GetRowId(), &tuple);
            if (result == NVMDB::HamStatus::OK) {
                found = true;
                break;
            }
            iter->Next();
        }

        if (found) {
            rowId = iter->GetRowId();
            iter->Next();

            result = NVMDB::HeapDelete(festate->mCurrTx, festate->mTable, rowId);
            if (result != NVMDB::HamStatus::OK) {
                ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE), errmsg("NVM Delete fail(%d)!", result)));
            }

            NVMDB_FDW::NVMDeleteTupleFromAllIndex(festate->mCurrTx, festate->mTable, &tuple, rowId);
            processCount++;
        } else {
            delete iter;
            return processCount;
        }
    }
}

void *NvmGetFdwState(ForeignScan *fscan, Oid tableId)
{
    NVMDB_FDW::NVMFdwState *festate = nullptr;
    NVMDB::Table *table = nullptr;
    table = NVMDB_FDW::NvmGetTableByOid(tableId);
    if (table != nullptr) {
        festate = NVMDB_FDW::NVMInitializeFdwState(fscan->fdw_private, &fscan->fdw_exprs, tableId);
        festate->mCurrTx = NVMDB_FDW::NVMGetCurrentTxContext();
        festate->mTable = table;
        festate->mRowIndex = 0;
    } else {
        Assert(false);
    }
    return festate;
}

void NvmIterDelete(void *iterPar)
{
    NVMDB_FDW::NvmFdwIter *iter = (NVMDB_FDW::NvmFdwIter *)iterPar;
    delete iter;
}
