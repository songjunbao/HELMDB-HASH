#include "heap/nvm_rowid_map.h"
#include "heap/nvm_heap.h"
#include <unordered_map>

namespace NVMDB {

DEFINE_int64(cache_size, 16384, "the max size of lru cache");
DEFINE_int64(cache_elasticity, 64, "the elasticity of lru cache");

/*
 * 这里的难点在于 extend 的时候，segments的指针会指向新的地址；而并发的读可能会读到旧的地址
 * 所以需要用一个 extend flag 来标记。读操作，在读 segments 前后会检查flag 是否有变化。
 * 如果有变化，需要重试一次。
 *
 * 正确性： 一个 segment 一旦创建，其地址不会改变，只要找到它起始地址就可以了。
 *        如果 reader 在读 segments 数组前后，flag没有变化，说明它读的segment值，是正确的。
 *        即使读操作结束之后，再扩展，也不影响该值。
 */
RowIdMapEntry *RowIdMap::GetSegment(int segId) {
    if (segId >= m_segmentCapacity.load()) {
        Extend(segId);
    }
    while (true) {
        uint32 flag = GetExtendVersion();
        auto segment = reinterpret_cast<RowIdMapEntry *>(m_segments.load()[segId]);
        uint32 newFlag = GetExtendVersion();
        if (segment == nullptr) {
            Extend(segId);
        } else if (newFlag == flag) {
            return segment;
        }
    }
}

void RowIdMap::Extend(int segId) {
    std::lock_guard<std::mutex> lockGuard(m_mutex);
    if (segId >= m_segmentCapacity.load()) {
        int newCap = m_segmentCapacity.load() * RowIdMapExtendFactor;
        while (newCap <= segId) {
            newCap *= RowIdMapExtendFactor;
        }
        auto newSegments = new RowIdMapEntry *[newCap];
        size_t memSize = newCap * sizeof(RowIdMapEntry *);
        int ret = memset_s(newSegments, memSize, 0, memSize);
        SecureRetCheck(ret);
        ret = memcpy_s(newSegments, memSize, m_segments, m_segmentCapacity.load() * sizeof(RowIdMapEntry *));
        SecureRetCheck(ret);

        SetExtendFlag();
        delete[] m_segments.load();
        m_segments.store(newSegments);
        ResetExtendFlag();
        m_segmentCapacity.store(newCap);
    }

    if (m_segments[segId] == nullptr) {
        auto *segment = new RowIdMapEntry[RowIdMapSegmentLen];
        size_t memSize = sizeof(RowIdMapEntry) * RowIdMapSegmentLen;
        int ret = memset_s(segment, memSize, 0, memSize);
        SecureRetCheck(ret);
        m_segments[segId] = segment;
    }
}

RowIdMapEntry *RowIdMap::GetEntry(RowId rowId, bool isRead) {
    int segId = (int)rowId / RowIdMapSegmentLen;
    RowIdMapEntry *segment = GetSegment(segId);
    RowIdMapEntry *entry = &segment[rowId % RowIdMapSegmentLen];

    if (!entry->IsValid()) {
        char *nvmTuple = m_rowidMgr->getNVMTupleByRowId(rowId, false);
        /* not valid row on nvm. */
        if (nvmTuple == nullptr) {
            DCHECK(isRead);
            return nullptr;
        }
        entry->Lock();  // init entry if not valid
        if (!entry->IsValid()) {
            entry->Init(nvmTuple);
        }
        entry->Unlock();
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    return entry;
}

static std::unordered_map<uint32, RowIdMap *> g_globalRowidMaps;
static std::mutex g_grimMtx;
thread_local std::unordered_map<uint32, RowIdMap *> g_localRowidMaps;

RowIdMap *GetRowIdMap(uint32 segHead, uint32 rowLen) {
    if (g_localRowidMaps.find(segHead) == g_localRowidMaps.end()) {
        // 本地缓存不存在对应表的segHead (page ID), 从全局中找 RowIdMap
        std::lock_guard<std::mutex> lockGuard(g_grimMtx);
        if (g_globalRowidMaps.find(segHead) == g_globalRowidMaps.end()) {
            // 为这张 table 创建 RowidMap, row id 为 table 中的行 id, 可以通过 row id 查找对应的 nvm tuple
            g_globalRowidMaps[segHead] = new RowIdMap(g_heapSpace, segHead, rowLen);
        }
        g_localRowidMaps[segHead] = g_globalRowidMaps[segHead];
    }
    RowIdMap *result = g_localRowidMaps[segHead];
    DCHECK(result->GetRowLen() == rowLen);  // rowLen 为表中一行的长度
    return result;
}

void InitGlobalRowIdMapCache() {
    g_globalRowidMaps.clear();
}

void InitLocalRowIdMapCache() {
    g_localRowidMaps.clear();
}

void DestroyGlobalRowIdMapCache() {
    for (auto entry : g_globalRowidMaps) {
        delete entry.second;
    }
    g_globalRowidMaps.clear();
}

void DestroyLocalRowIdMapCache() {
    g_localRowidMaps.clear();
}

}  // namespace NVMDB