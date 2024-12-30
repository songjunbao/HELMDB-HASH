#include "heap/nvm_tuple.h"
#include "heap/nvm_heap_undo.h"
#include "undo/nvm_undo_segment.h"

namespace NVMDB {

void RAMTuple::Serialize(char *buf, size_t bufLen) {
    m_nvmTuple.m_dataSize = m_rowLen;
    int ret = memcpy_s(buf, bufLen, &m_nvmTuple, NVMTupleHeadSize);
    SecureRetCheck(ret);
    ret = memcpy_s(buf + NVMTupleHeadSize, bufLen - NVMTupleHeadSize, m_rowData, m_rowLen);
    SecureRetCheck(ret);
}

void RAMTuple::Deserialize(const char *nvmTuple) {
    int ret = memcpy_s(&m_nvmTuple, sizeof(m_nvmTuple), nvmTuple, NVMTupleHeadSize);
    SecureRetCheck(ret);
    if (!m_nvmTuple.m_isUsed) {
        return;
    }
    DCHECK(m_rowLen == m_nvmTuple.m_dataSize);
    ret = memcpy_s(m_rowData, m_rowLen, nvmTuple + NVMTupleHeadSize, m_nvmTuple.m_dataSize);
    SecureRetCheck(ret);
}

void RAMTuple::FetchPreVersion(char* buffer) {
    DCHECK(!UndoRecPtrIsInValid(m_nvmTuple.m_prev));
    auto* undoRecordCache = reinterpret_cast<UndoRecord *>(buffer);
    GetUndoRecord(m_nvmTuple.m_prev, undoRecordCache);
    if (undoRecordCache->m_undoType == HeapUpdateUndo) {
        UndoUpdate(undoRecordCache, &this->m_nvmTuple, this->m_rowData);
    } else {
        Deserialize(undoRecordCache->data);
    }
}

}  // namespace NVMDB