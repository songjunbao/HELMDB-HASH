#include "nvm_access.h"
#include "heap/nvm_heap_undo.h"
#include "heap/nvm_heap_cache.h"

namespace NVMDB {

static inline bool CheckTxStatus(const Transaction *tx) {
    return tx->GetTxStatus() == TxStatus::WAIT_ABORT;
}

RowId HeapUpperRowId(const Table *table) {
    DCHECK(table->Ready());
    RowIdMap *rowIdMap = table->m_rowIdMap;
    return rowIdMap->getUpperRowId();
}

RowId HeapInsert(Transaction *tx, Table *table, RAMTuple *tuple) {
    DCHECK(table->GetRowLen() == tuple->getRowLen());
    if (CheckTxStatus(tx)) {
        LOG(ERROR) << "Insert cannot fail by default!";
        return InvalidRowId;
    }

    tx->PrepareUndo();
    DCHECK(table->Ready());
    RowIdMap *rowIdMap = table->m_rowIdMap;

    /* 分配一个RowId，这时候只是内存中的元数据修改了， NVM上的 NVMTUPLE_USED 标志位还没设上 */
    RowId rowId = rowIdMap->getNextEmptyRow();
    RowIdMapEntry *rowEntry = rowIdMap->GetEntry(rowId, false);

    PrepareInsertUndo(tx, table->SegmentHead(), rowId, tuple->getRowLen());

    rowEntry->Lock();
    // Write tuple to NVM; note marking head as used
    tuple->InitHead(tx->GetTxSlotLocation(), InvalidUndoRecPtr, true, false);
    // 如能写回DRAM cache, 写回 cache, 否则直接写入nvm
    const auto nvmFunc = [&](char* addr) {
        tuple->Serialize(addr, RealTupleSize(tuple->getRowLen()));
    };
    rowEntry->wrightThroughCache(nvmFunc, RealTupleSize(tuple->getRowLen()));
    // 写操作不需要加入到LRU
    // TLTupleCache::Touch(table->SegmentHead(), rowId, rowEntry);
    rowEntry->Unlock();

    tx->PushWriteSet(rowEntry);
    return rowId;
}

HamStatus HeapRead(const Transaction *tx, const Table *table, RowId rowId, RAMTuple *tuple) {
    DCHECK(table->m_rowLen == tuple->getRowLen());
    if (CheckTxStatus(tx)) {
        return HamStatus::WAIT_ABORT;
    }

    RowIdMap *rowIdMap = table->m_rowIdMap;
    RowIdMapEntry *rowEntry = rowIdMap->GetEntry(rowId, true);
    if (rowEntry == nullptr) {
        return HamStatus::READ_ROW_NOT_USED;
    }

    // 只读访问 dramCache
    rowEntry->Lock();
    // 读记录, 加入到LRU
    TLTupleCache::Touch(table->SegmentHead(), rowId, rowEntry);
    const auto* dramCache = rowEntry->loadDRAMCache<char>(RealTupleSize(tuple->getRowLen()));
    tuple->Deserialize(dramCache);
    rowEntry->Unlock();

    if (!tuple->IsUsed()) {
        return HamStatus::READ_ROW_NOT_USED;
    }
    while (true) {
        TMResult result = tx->VersionIsVisible(tuple->getNVMTuple());
        if (result == TMResult::OK || result == TMResult::SELF_UPDATED) {
            if (tuple->IsDeleted()) {
                return HamStatus::ROW_DELETED;
            }
            return HamStatus::OK;
        }
        if (result == TMResult::INVISIBLE || result == TMResult::ABORTED || result == TMResult::BEING_MODIFIED) {
            if (!tuple->HasPreVersion()) {
                return HamStatus::NO_VISIBLE_VERSION;
            }
            tuple->FetchPreVersion(tx->undoRecordCache);
            continue;
        }
        CHECK(false) << "should not enter here!";
    }
}

HamStatus HeapUpdate(Transaction *tx, Table *table, RowId rowId, RAMTuple *tuple) {
    DCHECK(table->GetRowLen() == tuple->getRowLen());
    if (CheckTxStatus(tx)) {
        return HamStatus::WAIT_ABORT;
    }

    tx->PrepareUndo();
    RowIdMap *rowIdMap = table->m_rowIdMap;
    RowIdMapEntry *rowEntry = rowIdMap->GetEntry(rowId, false);

    rowEntry->Lock();
    // 更新时读记录, 加入到LRU
    TLTupleCache::Touch(table->SegmentHead(), rowId, rowEntry);
    const auto* dramCache = rowEntry->loadDRAMCache<NVMTuple>(RealTupleSize(tuple->getRowLen()));
    TMResult result = tx->SatisfiedUpdate(*dramCache);
    if (result == TMResult::INVISIBLE || result == TMResult::BEING_MODIFIED) {
        rowEntry->Unlock();
        tx->WaitAbort();
        return HamStatus::UPDATE_CONFLICT;
    }

    DCHECK(result == TMResult::OK);
    if (dramCache->m_isDeleted) {
        rowEntry->Unlock();
        /* 一个”可见“的删除操作，说明尝试更新一个被删除的 tuple，需要报 error */
        tx->WaitAbort();
        return HamStatus::ROW_DELETED;
    }

    UndoColumnDesc *updatedCols = nullptr;
    uint32 updateCnt = 0;
    uint64 updateLen = 0;
    tuple->GetUpdatedCols(updatedCols, updateCnt, updateLen);
    UndoRecPtr undo_ptr = PrepareUpdateUndo(tx,
                                            table->SegmentHead(),
                                            rowId,
                                            *dramCache,
                                            UndoUpdatePara{updatedCols, updateCnt, updateLen});
    tuple->InitHead(tx->GetTxSlotLocation(), undo_ptr, dramCache->m_isUsed, dramCache->m_isDeleted);

    // inplace update
    auto* dramCacheAddr = rowEntry->loadDRAMCache<char>(RealTupleSize(tuple->getRowLen()));
    tuple->Serialize(dramCacheAddr, RealTupleSize(table->GetRowLen()));
    rowEntry->flushToNVM();
    rowEntry->Unlock();

    tx->PushWriteSet(rowEntry);
    return HamStatus::OK;
}

HamStatus HeapDelete(Transaction *tx, Table *table, RowId rowId) {
    if (CheckTxStatus(tx)) {
        return HamStatus::WAIT_ABORT;
    }

    tx->PrepareUndo();
    RowIdMap *rowIdMap = table->m_rowIdMap;
    RowIdMapEntry *rowEntry = rowIdMap->GetEntry(rowId, false);

    rowEntry->Lock();
    // 删除时读记录, 加入到LRU
    TLTupleCache::Touch(table->SegmentHead(), rowId, rowEntry);
    auto* dramCache = rowEntry->loadDRAMCache<NVMTuple>(RealTupleSize(table->GetRowLen()));
    TMResult result = tx->SatisfiedUpdate(*dramCache);
    if (result == TMResult::INVISIBLE || result == TMResult::BEING_MODIFIED) {
        rowEntry->Unlock();
        tx->WaitAbort();
        return HamStatus::UPDATE_CONFLICT;
    }

    DCHECK(result == TMResult::OK);
    if (dramCache->m_isDeleted) {
        rowEntry->Unlock();
        tx->WaitAbort();
        return HamStatus::ROW_DELETED;
    }
    // 记录delete的undo日志
    UndoRecPtr undo_ptr = PrepareDeleteUndo(tx, table->SegmentHead(), rowId, *dramCache);
    // 更新dramCache的header
    dramCache->m_isDeleted = true;
    dramCache->m_txInfo = tx->GetTxSlotLocation();
    dramCache->m_prev = undo_ptr;
    // 只用落盘header就可以, 因为data是空的
    rowEntry->flushHeaderToNVM();
    rowEntry->Unlock();

    tx->PushWriteSet(rowEntry);
    return HamStatus::OK;
}

}  // namespace NVMDB
