#pragma  once
#include "index/nvm_index_iter.h"
#include "index/nvm_index.h"

// PG fdw库的 log 和 google log 冲突
#undef LOG

#include "foreign/fdwapi.h"
#include "nodes/nodes.h"
#include "nodes/makefuncs.h"
#include "utils/numeric.h"
#include "utils/numeric_gs.h"
#include "pgstat.h"

constexpr int NVM_MAX_KEY_COLUMNS = 10U;

namespace NVMDB_FDW {

enum class KEY_OPER : uint8_t {
    READ_KEY_EXACT = 0,    // equal
    READ_KEY_OR_NEXT = 1,  // ge
    READ_KEY_AFTER = 2,    // gt
    READ_KEY_OR_PREV = 3,  // le
    READ_KEY_BEFORE = 4,   // lt
    READ_INVALID = 5,
};

struct NVMFdwConstraint {
    NVMDB::NVMIndex *mIndex;
    NVMDB_FDW::KEY_OPER mOper[NVM_MAX_KEY_COLUMNS];
    uint32 mMatchCount;
    List *mParentExprList;
    List *mExprList;
    double mCost;
    double mStartupCost;
};

class NvmFdwIter {
public:
    virtual void Next() = 0;

    virtual bool Valid() = 0;

    virtual NVMDB::RowId GetRowId() = 0;

    virtual ~NvmFdwIter() = default;
};

// 如果存在索引, 使用索引的迭代器
class NvmFdwIndexIter : public NvmFdwIter {
public:
    explicit NvmFdwIndexIter(NVMDB::NVMIndexIter *iter) noexcept : m_indexIter(iter) { }

    ~NvmFdwIndexIter() override { delete m_indexIter; }

    void Next() override { m_indexIter->Next(); }

    bool Valid() override { return m_indexIter->Valid(); }

    NVMDB::RowId GetRowId() override { return m_indexIter->Curr(); }

private:
    NVMDB::NVMIndexIter *m_indexIter;
};

// 如果不存在索引, 顺序查找
class NvmFdwSeqIter : public NvmFdwIter {
public:
    explicit NvmFdwSeqIter(NVMDB::RowId max) noexcept : m_maxRowId(max) { }

    void Next() override { m_curRowId++; }

    bool Valid() override { return m_curRowId < m_maxRowId; }

    NVMDB::RowId GetRowId() override { return m_curRowId; }

private:
    NVMDB::RowId m_curRowId = 0;
    const NVMDB::RowId m_maxRowId;
};

// 检测是否能用索引优化
class NvmMatchIndex {
public:
    NvmMatchIndex() noexcept {
        for (uint j = 0; j < NVM_MAX_KEY_COLUMNS; j++) {
            m_colMatch[j] = nullptr;
            m_parentColMatch[j] = nullptr;
            m_opers[j] = KEY_OPER::READ_INVALID;
        }
    }

    ~NvmMatchIndex() noexcept = default;

    NVMDB::NVMIndex *m_ix = nullptr;
    Expr *m_colMatch[NVM_MAX_KEY_COLUMNS]{};
    Expr *m_parentColMatch[NVM_MAX_KEY_COLUMNS]{};
    KEY_OPER m_opers[NVM_MAX_KEY_COLUMNS]{};

    bool SetIndexColumn(uint32 colNum, KEY_OPER op, Expr *expr, Expr *parent) {
        bool ret = false;
        uint32 colCount = m_ix->GetColCount();
        const NVMDB::IndexColumnDesc *indexDesc = m_ix->GetIndexDesc();

        for (uint32 i = 0; i < colCount; i++) {
            if (indexDesc[i].m_colId != colNum) {
                continue;
            }
            if (m_colMatch[i] == nullptr) {
                m_parentColMatch[i] = parent;
                m_colMatch[i] = expr;
                m_opers[i] = op;
                ret = true;
                break;
            }
            if (op == KEY_OPER::READ_KEY_EXACT && op != m_opers[i]) {
                m_parentColMatch[i] = parent;
                m_colMatch[i] = expr;
                m_opers[i] = op;
                ret = true;
                break;
            }
        }
        return ret;
    }
};

// 一组可供搜索的索引, 从中选择最佳的
class NvmMatchIndexArr {
public:
    NvmMatchIndexArr() noexcept {
        for (auto & i : m_idx) {
            i = nullptr;
        }
    }

    ~NvmMatchIndexArr() noexcept {
        Clear(true);
    }

    void Clear(bool release = false) {
        for (auto & i : m_idx) {
            if (i) {
                if (release) {
                    pfree(i);
                }
                i = nullptr;
            }
        }
    }
    NvmMatchIndex *m_idx[NVM_MAX_KEY_COLUMNS]{};
};

struct NVMFdwState {
    CmdType mCmdOper;
    bool mAllocInScan;
    Oid mForeignTableId;
    AttrNumber mNumAttrs;
    AttrNumber mCtidNum;
    uint16_t mNumExpr;
    uint8 *mAttrsUsed;
    uint8 *mAttrsModified;
    List *mLocalConds;
    NVMFdwConstraint mConst;
    NVMFdwConstraint mConstPara;
    NVMDB::Table *mTable;
    NVMDB::Transaction *mCurrTx;
    NVMDB::RowId mRowIndex;
    bool mCursorOpened;
    NVMDB_FDW::NvmFdwIter *mIter;
};

constexpr char *NVM_REC_TID_NAME = "ctid";

enum class NVM_ERRCODE {
    NVM_SUCCESS = 0,
    NVM_ERRCODE_INPUT_PARA_ERROR,
    NVM_ERRCODE_UNSUPPORTED_COL_TYPE,
    NVM_ERRCODE_NO_MEM,
    NVM_ERRCODE_TABLE_NOT_FOUND,
    NVM_ERRCODE_INDEX_NOT_FOUND,
    NVM_ERRCODE_COL_NOT_FOUND,
    NVM_ERRCODE_INDEX_TYPE_NOT_SUPPORT,
    NVM_ERRCODE_INDEX_NOT_SUPPORT_NULL,
    NVM_ERRCODE_INDEX_SIZE_EXC_LIMIT,
    NVM_ERRCODE_COL_COUNT_EXC_LIMIT,
    NVM_ERRCODE_INDEX_NOT_SUPPORT_EXPR,
    NVM_ERRCODE_INVALID
};

enum class FDW_LIST_TYPE {
    FDW_LIST_STATE = 1,
    FDW_LIST_BITMAP = 2,
};

constexpr uint64 MAX_VARCHAR_LEN = 1024;
// Decimal representation
constexpr int DECIMAL_POSITIVE = 0x01;
constexpr int DECIMAL_NEGATIVE = 0x02;
constexpr int DECIMAL_NAN = 0x04;
constexpr int DECIMAL_MAX_DIGITS = 16;
constexpr int NVM_NUMERIC_MAX_PRECISION = DECIMAL_MAX_DIGITS * 4;

struct __attribute__((packed)) DecimalHdrSt {
    uint8 m_flags;
    uint16 m_weight;
    uint16 m_scale;
    uint16 m_ndigits;
};

struct __attribute__((packed)) DecimalSt {
    DecimalHdrSt m_hdr;
    uint16 m_round;
    uint16 m_digits[0];
};

#define DECIMAL_MAX_SIZE (sizeof(DecimalSt) + DECIMAL_MAX_DIGITS * sizeof(uint16))
#define DECIMAL_SIZE(d) (sizeof(DecimalSt) + (d)->m_hdr.m_ndigits * sizeof(int16))

}  // namespace NVMDB_FDW
