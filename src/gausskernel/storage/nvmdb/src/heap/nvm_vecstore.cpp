#include "heap/nvm_vecstore.h"
#include "heap/nvm_tuple.h"
#include "heap/nvm_heap_cache.h"
#include "nvmdb_thread.h"
#include <memory>

namespace NVMDB {

VecStore::VecStore(TableSpace *tableSpace, uint32 segHead, uint32 tuplesPerExtent) {
    m_tableSpace = tableSpace;  // 可以通过 table space 搜索表
    m_segHead = segHead;    // 一张表的 segment head 对应的 page ID
    m_tuplesPerExtent = tuplesPerExtent;   // 每个extent存的元组数量

    // 一共有多少个目录
    auto spaceCount = m_tableSpace->getDirConfig()->size();
    CHECK(spaceCount > 0);
    m_gbm.resize(spaceCount);
    // 根据表内最多多少行, 换算出每个目录最多能保存多少个 pages
    const uint32 pagesPerDir = MaxRowId / m_tuplesPerExtent / spaceCount;
    for (uint32 i = 0; i < spaceCount; i++) {
        // 内存中, 每个目录初始化一个bit map 并置为0, 每个page一个bit
        m_gbm[i] = std::make_unique<GlobalBitMap>(pagesPerDir);
    }
}

RowId VecStore::tryNextRowid() const {
    // 在 table中锁定一个 extent (一组连续的pages), 用以写入数据
    auto *localTableCache = TLTableCache::GetThreadLocalTableCache(m_segHead);

    // 1. 从 RowID Cache 中找，是否有自己之前删过的。
    RowId rid = localTableCache->m_rowidCache.pop();
    if (RowIdIsValid(rid)) {
        return rid;
    }

    while (true) {
        // 2. 从 Range 中找从来没有用过的。
        rid = localTableCache->m_range.next();
        if (RowIdIsValid(rid)) {
            return rid;
        }

        // 3. 从 GlobalBitMap中分配一个新的Range
        auto spaceCount = m_tableSpace->getDirConfig()->size();
        uint32 dirSeq = GetCurrentGroupId() % spaceCount;
        uint32 localBit = m_gbm[dirSeq]->SyncAcquire(); // 表分区对应的未用过的位
        uint32 globalBit = dirSeq + spaceCount * localBit;    // 表全局对应的未用过的位
        localTableCache->m_range.setRange(globalBit * m_tuplesPerExtent, (globalBit + 1) * m_tuplesPerExtent);
    }
    CHECK(false);
}

}  // namespace NVMDB