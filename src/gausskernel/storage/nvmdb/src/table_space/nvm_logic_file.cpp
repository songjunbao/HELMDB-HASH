#include "table_space/nvm_logic_file.h"
#include "common/nvm_cfg.h"
#include <libpmem.h>

namespace NVMDB {

bool LogicFile::mMapFile(uint32 segmentId, bool init) {
    // if is mounted, return
    if (m_segmentAddr.size() > segmentId && m_segmentAddr[segmentId] != nullptr) {
        return true;
    }
    // create file if not exist
    int flags = init ? PMEM_FILE_CREATE : 0;
    size_t actualSegmentSize;
    int isPMem;
    auto fileName = segmentFilename(segmentId);
    void *nvmAddr = pmem_map_file(fileName.data(),
                                  m_segmentSize,
                                  flags,
                                  0666,
                                  &actualSegmentSize,
                                  &isPMem);
    // NVM挂载失败
    if (!isPMem || nvmAddr == nullptr || actualSegmentSize != m_segmentSize) {
        if (!init) {
            return false;
        }
        CHECK(false) << "Cannot map PMem file!";
    }
    // 添加nvmAddr到m_segmentAddr
    if (m_segmentAddr.size() <= segmentId) {
        m_segmentAddr.resize(segmentId + 1);
    }
    m_segmentAddr[segmentId] = nvmAddr;
    return true;
}

void LogicFile::unMMapFile(uint32 segmentId, bool destroy) {
    if (m_segmentAddr.size() <= segmentId || m_segmentAddr[segmentId] == nullptr) {
        return;
    }
    pmem_unmap(m_segmentAddr[segmentId], m_segmentSize);
    m_segmentAddr[segmentId] = nullptr;
    if (!destroy) {
        return;
    }
    unlink(segmentFilename(segmentId).data());
}

}  // namespace NVMDB