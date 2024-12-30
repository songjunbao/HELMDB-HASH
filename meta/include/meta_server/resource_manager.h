#pragma once

#include <map>
#include <memory>
#include "proto/task_2.pb.h"
#include "meta_common.h"
#include "meta_map.h"

using meta::NodeLoadInfo;
using meta::NodeLoadInfoMap;
using meta::MetaMap;

class ResourceManager {
public:
    static ResourceManager& instance();
    uint64_t update_node_load_info(ServerID server_id, const NodeLoadInfo& NodeLoadInfo);

private:
    ResourceManager();

    // switch to meta_map
    // std::map<ServerID, NodeLoadInfo> nodeLoadInfoMap;
    MetaMap<ServerID, NodeLoadInfo> nodeLoadInfoMap;
};