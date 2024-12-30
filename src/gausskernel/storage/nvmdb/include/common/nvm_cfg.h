#ifndef NVMDB_CFG_H
#define NVMDB_CFG_H

#include "common/nvm_types.h"
#include <vector>
#include <string>
#include <memory>

namespace NVMDB {

// 最多四路处理器
static constexpr int NVMDB_MAX_GROUP = 4;

// Always greater than the number of concurrent connections
static constexpr uint32 NVMDB_MAX_THREAD_NUM = 1024;

// for undo
static constexpr int NVMDB_UNDO_SEGMENT_NUM = 2048;
static_assert(NVMDB_UNDO_SEGMENT_NUM >= NVMDB_MAX_THREAD_NUM, "");

// for pactree oplog
static constexpr int NVMDB_NUM_LOGS_PER_THREAD = 512;
static constexpr int NVMDB_OPLOG_WORKER_THREAD_PER_GROUP = 1;
static constexpr int NVMDB_OPLOG_QUEUE_MAX_CAPACITY = 10000;

class DirectoryConfig {
public:
    explicit DirectoryConfig(const std::string& dirPathsString, bool init=false);

    [[nodiscard]] const auto& getDirPaths() const { return m_dirPaths; }

    [[nodiscard]] auto size() const { return m_dirPaths.size(); }

    const std::string& getDirPathByIndex(size_t indexHint) {
        return m_dirPaths[indexHint % m_dirPaths.size()];
    }

private:
    std::vector<std::string> m_dirPaths;
};

extern std::shared_ptr<NVMDB::DirectoryConfig> g_dir_config;

}  // namespace NVMDB

#endif