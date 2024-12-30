#ifndef NVMDB_ROWID_MGR_H
#define NVMDB_ROWID_MGR_H

#include "table_space/nvm_table_space.h"
#include "heap/nvm_tuple.h"

namespace NVMDB {

static const ExtentSizeType HEAP_EXTENT_SIZE = EXT_SIZE_2M;

class RowIDMgr {
public:
    // 为 Table 提供进一步抽象 rowId 为一个 Table 中的行 ID
    // segHead: Table 的入口, 为 Page id 类型, 存储所有 leaf extent 对应的 page ids
    RowIDMgr(TableSpace *tableSpace, uint32 segHead, uint32 tupleLen)
        : m_tableSpace(tableSpace),
          m_segHead(segHead),
          m_tupleLen(tupleLen + NVMTupleHeadSize) {
        // 一个table segment的总逻辑空间 - header 除以每个tuple长度
        m_tuplesPerExtent = GetExtentSize(HEAP_EXTENT_SIZE) / m_tupleLen;
    }

    // 根据 rowid 读取NVM中Table对应的记录
    char *getNVMTupleByRowId(RowId rowId, bool append) {
        // pageId, page_offset
        const uint32 leafExtentId = rowId / m_tuplesPerExtent;
        const uint32 leafPageOffset = rowId % m_tuplesPerExtent;

        uint32 *extentIds = GetLeafPageExtentIds();
        /* 1. check leaf page existing. If not, try to allocate a new page */
        if (!NVMPageIdIsValid(extentIds[leafExtentId])) {    // pageId
            if (!append) {  // 只读请求, 不需要创建 leafPage
                return nullptr;
            }
            UpdateMaxPageId(leafExtentId);
            tryAllocNewPage(leafExtentId);
        }

        uint32 pageId = extentIds[leafExtentId];
        DCHECK(NVMPageIdIsValid(pageId));
        char *leafPage = m_tableSpace->getNvmAddrByPageId(pageId);
        char *leafData = (char *)GetExtentAddr(leafPage);
        char *tuple = leafData + leafPageOffset * m_tupleLen;

        return tuple;
    }

    // upper bound RowId in highest allocated range
    inline RowId getUpperRowId() const { return (GetMaxPageId() + 1) * m_tuplesPerExtent; }

    // 每个heap page extent能存储的tuple数量
    [[nodiscard]] inline uint32 getTuplesPerExtent() const { return m_tuplesPerExtent; };

protected:
    // Table Segment Header 存储 MaxPageNum 和 PageMap
    uint32 *GetLeafPageExtentIds() {
        char *rootPage = m_tableSpace->getNvmAddrByPageId(m_segHead);
        /* NVMPageHeader + MaxPageNum + Page Maps */
        return (uint32 *)GetExtentAddr(rootPage) + 1;
    }

    // pageId: 当前 Table 分配的 extent id (不一定最大)
    inline void UpdateMaxPageId(uint32 pageId) {
        char *rootPage = m_tableSpace->getNvmAddrByPageId(m_segHead);
        auto *maxPageId = (uint32 *)GetExtentAddr(rootPage);
        if (*maxPageId < pageId) {
            *maxPageId = pageId;
        }
    }

    inline uint32 GetMaxPageId() const {
        char *rootPage = m_tableSpace->getNvmAddrByPageId(m_segHead);
        return *(uint32 *)GetExtentAddr(rootPage);
    }

    // 基于 m_segHead 分配新的 extent, 并存储在 m_segHead 中
    void tryAllocNewPage(uint32 leafExtentId) {
        std::lock_guard<std::mutex> lock_guard(m_tableSpaceMutex);
        auto *extentIds = GetLeafPageExtentIds();
        if (NVMPageIdIsValid(extentIds[leafExtentId])) {
            return;
        }
        // use leafExtentId as a hint for space idx
        // leaf page idx is logic page number. allocate physical page from space idx.
        extentIds[leafExtentId] = m_tableSpace->allocNewExtent(HEAP_EXTENT_SIZE, m_segHead, leafExtentId);
    }

private:
    std::mutex m_tableSpaceMutex;
    TableSpace *m_tableSpace;   // Table space, 保存真正的文件
    const uint32 m_segHead;   // 当前 Table 的 page id 入口
    const uint32 m_tupleLen;  // 实际上每行占用的NVM的长度, 每行定长
    uint32 m_tuplesPerExtent;  // 每个Table segment能保存的数组数量
};

}  // namespace NVMDB

#endif  // NVMDB_ROWID_MGR_H