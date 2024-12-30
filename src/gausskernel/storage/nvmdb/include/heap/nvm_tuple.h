#ifndef NVMDB_TUPLE_H
#define NVMDB_TUPLE_H

#include "common/nvm_cfg.h"
#include "undo/nvm_undo_ptr.h"
#include "glog/logging.h"
#include <bitset>

namespace NVMDB {
static constexpr uint32 NVMDB_TUPLE_MAX_COL_COUNT = 64;

static constexpr uint32 NVM_MAX_COLUMN_NAME_LEN = 84;

typedef enum ColumnType : uint32 {
    COL_TYPE_INT = 0,
    COL_TYPE_LONG = 1,
    COL_TYPE_FLOAT = 2,
    COL_TYPE_VARCHAR = 3,
    COL_TYPE_UNSIGNED_LONG = 4,
    /* above required in tpc-c. */
    COL_TYPE_DOUBLE = 5,
    COL_TYPE_SHORT = 6,
    COL_TYPE_TINY = 7,
    COL_TYPE_DATE = 8,
    COL_TYPE_TIME = 9,
    COL_TYPE_CHAR = 10,
    COL_TYPE_TIMESTAMP = 11,
    COL_TYPE_TIMESTAMPTZ = 12,
    COL_TYPE_INTERVAL = 13,
    COL_TYPE_TINTERVAL = 14,
    COL_TYPE_TIMETZ = 15,
    COL_TYPE_DECIMAL = 16,
    COL_TYPE_INVALID
} ColumnType;

// 表格中每一列的定义
struct ColumnDesc {
    // NVM 数据类型
    ColumnType m_colType;
    // PG数据类型, 可以和m_colType相互转换
    uint32 m_colOid;
    // 类型占用空间
    uint64 m_colLen;
    // 这一列在一行中的数据偏移量
    uint64 m_colOffset;
    // 是否允许为空
    bool m_isNotNull;
    // 列名
    char m_colName[NVM_MAX_COLUMN_NAME_LEN];
};

struct UndoColumnDesc {
    uint64 m_colOffset;
    uint64 m_colLen;
};

struct UndoUpdatePara {
    UndoColumnDesc *m_updatedCols{nullptr};
    uint32 m_updateCnt{0};
    uint64 m_updateLen{0};
};

using NvmNullType = std::bitset<NVMDB_TUPLE_MAX_COL_COUNT>;

struct NVMTuple {
    // 事务状态信息，如果事务已经提交，是csn；如果正在执行，是TxSlotPtr (TSP)的位置信息。
    // 这里利用了 TSP 只有32位，所以首bit肯定是0。同时设计最小的 CSN 为 1<<63, CSN 的首bit肯定是1.
    // 所以可以用第一个bit区分是CSN 还是 TSP
    uint64 m_txInfo;
    UndoRecPtr m_prev;          // 指向undo区域的旧版本
    bool m_isUsed : 8;
    bool m_isDeleted : 8;
    uint32 m_dataSize;          // tuple 中数据的长度, 在序列化前设置
    NvmNullType m_nullBitmap;   // 哪一个列是空的
    static_assert(BIS_PER_BYTE * sizeof(m_nullBitmap) >= NVMDB_TUPLE_MAX_COL_COUNT, "");
    char m_data[0];             // 数据指针, 只读 (不能在DRAM中写)
};

static_assert(sizeof(NVMTuple) % 32 == 0, "");

class RAMTuple {
private:
    // 该tuple的定义
    const ColumnDesc *const m_rowDes;
    // 在初始化时候设置, 是固定长的
    const uint32 m_rowLen;

    // tuple更新相关
    std::unique_ptr<UndoColumnDesc[]> m_updatedCols;
    uint32 m_updateCnt;
    uint64 m_updateLen;

    // 缓存在内存中
    NVMTuple m_nvmTuple;
    char* m_rowData;
    // 该tuple是否拥有 m_rowData
    const bool m_holdRowData;

public:
    friend class DRAMIndexTuple;

    struct ColumnUpdate {
        uint32 m_colId = 0;
        char *m_colData = nullptr;
    };

    RAMTuple(const ColumnDesc *rowDes, uint64 rowLen)
        : m_rowDes(rowDes), m_rowLen(rowLen), m_updateCnt(0), m_updateLen(0), m_holdRowData(true) {
        DCHECK(rowLen <= MAX_TUPLE_LEN);
        m_updatedCols = std::make_unique<UndoColumnDesc[]>(rowLen);
        // 创建 row data
        m_rowData = new char[rowLen];
        auto ret = memset_s(m_rowData, rowLen, 0, rowLen);
        SecureRetCheck(ret);
    }

    RAMTuple(const ColumnDesc *rowDes, uint64 rowLen, char *rowData) noexcept
        : m_rowDes(rowDes), m_rowLen(rowLen),
          m_updatedCols(nullptr),
          m_updateCnt(0),
          m_updateLen(0), m_holdRowData(false) {
        m_rowData = rowData;
    }

    ~RAMTuple() {
        if (m_holdRowData) {
            delete[] m_rowData;
        }
    }

    void UpdateCols(ColumnUpdate *updates, uint32 update_cnt) {
        DCHECK(updates != nullptr && update_cnt != 0);
        m_updateCnt = update_cnt;
        m_updateLen = 0;
        for (uint32 i = 0; i < update_cnt; i++) {
            uint32 colId = updates[i].m_colId;
            uint64 colOffset = m_rowDes[colId].m_colOffset;
            uint64 colLen = m_rowDes[colId].m_colLen;
            m_updateLen += colLen;
            m_updatedCols[i].m_colOffset = colOffset;
            m_updatedCols[i].m_colLen = colLen;
            int ret = memcpy_s(m_rowData + colOffset, m_rowLen - colOffset, updates[i].m_colData, colLen);
            SecureRetCheck(ret);
        }
    }

    inline void UpdateCol(const uint32 colId, char *const colData) {
        ColumnUpdate _updates{colId, colData};
        UpdateCols(&_updates, 1);
    }

    inline void UpdateColsRecord(ColumnUpdate *updates, uint32 updateCount) {
        DCHECK(updates != nullptr && updateCount != 0);
        m_updateCnt = updateCount;
        m_updateLen = 0;
        uint32 colId = 0;
        uint64 colOffset = 0;
        uint64 colLen = 0;
        for (uint32 i = 0; i < updateCount; i++) {
            colId = updates[i].m_colId;
            colOffset = m_rowDes[colId].m_colOffset;
            colLen = m_rowDes[colId].m_colLen;
            m_updateLen += colLen;
            m_updatedCols[i].m_colOffset = colOffset;
            m_updatedCols[i].m_colLen = colLen;
        }
    }

    inline void UpdateColInc(const uint32 colId, char *const colData, uint64 len) {
        uint64 colOffset = m_rowDes[colId].m_colOffset;
        uint64 colLen = m_rowDes[colId].m_colLen;
        m_updateLen += colLen;
        m_updatedCols[m_updateCnt].m_colOffset = colOffset;
        m_updatedCols[m_updateCnt].m_colLen = colLen;
        int ret = memcpy_s(m_rowData + colOffset, m_rowLen - colOffset, colData, len);
        SecureRetCheck(ret);

        m_updateCnt++;
    }

    inline void GetUpdatedCols(UndoColumnDesc *&updatedCols, uint32 &updateCnt, uint64 &updateLen) const {
        updatedCols = m_updatedCols.get();
        updateCnt = m_updateCnt;
        updateLen = m_updateLen;
    }

    void SetCols(ColumnUpdate *updates, uint32 updateCnt) const {
        DCHECK(updates != nullptr && updateCnt != 0);
        for (uint32 i = 0; i < updateCnt; i++) {
            uint32 colId = updates[i].m_colId;
            int ret = memcpy_s(m_rowData + m_rowDes[colId].m_colOffset, m_rowLen - m_rowDes[colId].m_colOffset,
                               updates[i].m_colData, m_rowDes[colId].m_colLen);
            SecureRetCheck(ret);
        }
    }

    void GetCols(ColumnUpdate *updates, uint32 updateCnt) const {
        DCHECK(updates != nullptr && updateCnt != 0);
        for (uint32 i = 0; i < updateCnt; i++) {
            uint32 col_id = updates[i].m_colId;
            int ret = memcpy_s(updates[i].m_colData, m_rowDes[col_id].m_colLen,
                               m_rowData + m_rowDes[col_id].m_colOffset, m_rowDes[col_id].m_colLen);
            SecureRetCheck(ret);
        }
    }

    // copy colData to m_rowData + m_rowDes[colId].m_colOffset
    inline void SetCol(const uint32 colId, char *const colData) {
        int ret = memcpy_s(m_rowData + m_rowDes[colId].m_colOffset, m_rowLen - m_rowDes[colId].m_colOffset,
                           colData, m_rowDes[colId].m_colLen);
        SecureRetCheck(ret);
    }

    // copy colData to m_rowData + m_rowDes[colId].m_colOffset
    inline void SetCol(const uint32 colId, char *const colData, uint64 len) {
        DCHECK(len <= m_rowDes[colId].m_colLen);
        int ret = memcpy_s(m_rowData + m_rowDes[colId].m_colOffset, m_rowLen - m_rowDes[colId].m_colOffset,
                           colData, len);
        SecureRetCheck(ret);
    }

    inline void GetCol(const uint32 colId, char *const colData) const {
        int ret = memcpy_s(colData, m_rowDes[colId].m_colLen, m_rowData + m_rowDes[colId].m_colOffset,
                           m_rowDes[colId].m_colLen);
        SecureRetCheck(ret);
    }

    inline char *GetCol(const uint32 colId) const {
        return m_rowData + m_rowDes[colId].m_colOffset;
    }

    // copy tuple->m_rowData to m_rowData
    inline void CopyRow(const RAMTuple& tuple) {
        DCHECK(tuple.m_rowLen == m_rowLen);
        int ret = memcpy_s(m_rowData, m_rowLen, tuple.m_rowData, tuple.m_rowLen);
        SecureRetCheck(ret);
    }

    inline bool EqualRow(RAMTuple *tuple) const {
        return (memcmp(m_rowData, tuple->m_rowData, m_rowLen) == 0);
    }

    inline bool ColEqual(const uint32 colId, char *const colData) const {
        return (memcmp(m_rowData + m_rowDes[colId].m_colOffset, colData, m_rowDes[colId].m_colLen) == 0);
    }

    [[nodiscard]] bool HasPreVersion() const {
        return !UndoRecPtrIsInValid(m_nvmTuple.m_prev);
    }

    void InitHead(uint64 txInfo, UndoRecPtr prev, bool isUsed, bool isDeleted) {
        m_nvmTuple.m_txInfo = txInfo;
        m_nvmTuple.m_prev = prev;
        m_nvmTuple.m_isUsed = isUsed;
        m_nvmTuple.m_isDeleted = isDeleted;
        m_nvmTuple.m_dataSize = m_rowLen;
    }

    void Serialize(char *buf, size_t bufLen);

    void Deserialize(const char *buf);

    void FetchPreVersion(char* buffer);

    [[nodiscard]] inline bool IsUsed() const { return m_nvmTuple.m_isUsed; }

    [[nodiscard]] inline bool IsDeleted() const { return m_nvmTuple.m_isDeleted; }

    void setUsed(bool isUsed) { m_nvmTuple.m_isUsed = isUsed; }

    void setDeleted(bool isDeleted) { m_nvmTuple.m_isDeleted = isDeleted; }

    [[nodiscard]] inline bool IsNull(uint32 colId) const { return m_nvmTuple.m_nullBitmap[colId]; }

    [[nodiscard]] inline void SetNull(uint32 colOd, bool isNull) { m_nvmTuple.m_nullBitmap.set(colOd, isNull); }

    inline uint64 getRowLen() const { return m_rowLen; }

    inline const auto& getNVMTuple() const { return m_nvmTuple; }
};

static const int NVMTupleHeadSize = sizeof(NVMTuple);

inline size_t RealTupleSize(size_t row_len) {
    return row_len + NVMTupleHeadSize;
}

}  // namespace NVMDB

#endif // NVMDB_TUPLE_H