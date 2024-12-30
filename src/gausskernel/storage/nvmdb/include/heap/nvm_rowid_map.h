#ifndef NVMDB_ROWID_MAP_H
#define NVMDB_ROWID_MAP_H

#include "table_space/nvm_table_space.h"
#include "heap/nvm_tuple.h"
#include "nvm_vecstore.h"
#include "common/nvm_spinlock.h"
#include <atomic>
#include <functional>

namespace NVMDB {

class RowIdMapEntry {
public:
    inline void Lock() { m_mutex.lock(); }

    [[nodiscard]] inline bool TryLock() { return m_mutex.try_lock(); }

    inline void Unlock() { m_mutex.unlock(); }

    inline bool IsValid() const { return m_isTupleValid; }

    template <typename T=NVMTuple>
    T *loadDRAMCache(size_t tupleSize) {
        DCHECK(tupleSize <= MAX_TUPLE_LEN);
        // 防止段错误, 重新加载tuple
        if (m_dramCache.size() < tupleSize) {
            clearCache();
        }
        if (m_dramCache.empty()) {
            m_dramCache.resize(tupleSize);
            errno_t ret = memcpy_s(m_dramCache.data(), tupleSize, m_nvmAddr, tupleSize);
            SecureRetCheck(ret);
        }
        return reinterpret_cast<T *>(m_dramCache.data());
    }

    void flushToNVM() {
        if (m_dramCache.empty()) {
            LOG(ERROR) << "DRAM cache is empty!";
            return;
        }
        errno_t ret = memcpy_s(m_nvmAddr, m_dramCache.size(), m_dramCache.data(), m_dramCache.size());
        SecureRetCheck(ret);
    }

    void flushHeaderToNVM() {
        if (m_dramCache.size() < NVMTupleHeadSize) {
            LOG(ERROR) << "DRAM cache is empty!";
            return;
        }
        errno_t ret = memcpy_s(m_nvmAddr, NVMTupleHeadSize, m_dramCache.data(), NVMTupleHeadSize);
        SecureRetCheck(ret);
    }

    // 针对非read modify write设计, 直接写入
    void wrightThroughCache(const std::function<void(char*)>& nvmFunc, size_t syncSize) {
        // 如果缓存size不够, 清理缓存
        if (syncSize < m_dramCache.size()) {
            clearCache();
        }
        // 对于未读缓存的tuple, 直接写NVM
        if (m_dramCache.empty()) {
            nvmFunc(m_nvmAddr);
            return;
        }
        // 对于读缓存的tuple, 先写dram之后刷盘
        nvmFunc(m_dramCache.data());
        errno_t ret = memcpy_s(m_nvmAddr, syncSize, m_dramCache.data(), syncSize);
        SecureRetCheck(ret);
    }

    void Init(char* nvmAddr) {
        m_nvmAddr = nvmAddr;
        clearCache();
        std::atomic_thread_fence(std::memory_order_release);
        m_isTupleValid = true;
    }

public:
    // 每张表, 每个线程一个LRU, 引用计数相关
    int increaseReference() { return m_referenceCount.fetch_add(1); }

    int decreaseReference() { return m_referenceCount.fetch_sub(1); }

    int getReferenceCount(std::memory_order memoryOrder) const { return m_referenceCount.load(memoryOrder); }

    // 清理缓存 (此操作会释放空间)
    inline void clearAndShrinkCache() { std::vector<char>().swap(m_dramCache); }

protected:
    // 清理缓存 (此操作不释放空间)
    inline void clearCache() { m_dramCache.clear(); }

private:
    PassiveSpinner42 m_mutex;
    bool m_isTupleValid = false;

private:
    char *m_nvmAddr = nullptr;
    // 引用计数, 当变为0时销毁对应缓存
    std::atomic<int> m_referenceCount = {0};
    std::vector<char> m_dramCache = {};
};

namespace {
constexpr int RowIdMapExtendFactor = 2;
constexpr int RowIdMapSegmentLen = 128 * 1024;
}

class RowIdMap {
public:
    // rowLen: 定长存储, 每一行数据的最大长度
    RowIdMap(TableSpace *tableSpace, uint32 segHead, uint32 rowLen) : m_rowLen(rowLen), m_extendFlag(0) {
        m_segmentCapacity = 16; // 初始段数, 在不够用时原子性扩展
        m_segments.store(new RowIdMapEntry *[m_segmentCapacity]());
        m_rowidMgr = new RowIDMgr(tableSpace, segHead, rowLen);
        auto tuplesPerExtent = m_rowidMgr->getTuplesPerExtent();
        m_vecStore = new VecStore(tableSpace, segHead, tuplesPerExtent);
    }

    RowId getNextEmptyRow() {
        while (true) {
            // 尝试在Table对应的 segments 中找到一个空的 rowId
            RowId rowId = m_vecStore->tryNextRowid();
            // 对应的页面是否存在。以及是否有人占，重启之后需要这种方法来确认。
            char *tuple = m_rowidMgr->getNVMTupleByRowId(rowId, true);
            const auto *tupleHead = reinterpret_cast<NVMTuple *>(tuple);
            DCHECK(tupleHead != nullptr);
            if (tupleHead->m_isUsed) {
                continue;   // 重试, row被占用
            }
            // 确定这个RowId对应的 heap 为空
            return rowId;
        }
    }

    inline uint32 GetRowLen() const { return m_rowLen; }

    RowId getUpperRowId() const { return m_rowidMgr->getUpperRowId(); }

    RowIdMapEntry *GetEntry(RowId rowId, bool isRead);

protected:
    void SetExtendFlag() {
        m_extendFlag.fetch_add(1);
    }

    void ResetExtendFlag() {
        m_extendFlag.fetch_add(1);
    }

    uint32 GetExtendVersion() {
        uint32 res = m_extendFlag.load();
        while (res & 1) {
            res = m_extendFlag.load();
        }
        return res;
    }

    RowIdMapEntry *GetSegment(int segId);

    void Extend(int segId);

private:
    std::atomic<uint32> m_extendFlag;
    std::atomic<RowIdMapEntry **> m_segments{};
    std::atomic<int> m_segmentCapacity{};

    VecStore *m_vecStore;

    // 为 Table 提供进一步抽象 rowId 为一个 Table 中的行 ID
    RowIDMgr *m_rowidMgr = nullptr;

    uint32 m_rowLen;
    std::mutex m_mutex;
};

RowIdMap *GetRowIdMap(uint32 segHead, uint32 row_len);

void InitGlobalRowIdMapCache();

void InitLocalRowIdMapCache();

void DestroyGlobalRowIdMapCache();

void DestroyLocalRowIdMapCache();

}  // namespace NVMDB

#endif  // NVMDB_ROWID_MAP_H
