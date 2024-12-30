#include "resource_manager.h"

ResourceManager& ResourceManager::instance() {
    static ResourceManager instance;
    return instance;
}

ResourceManager::ResourceManager() {
}

uint64_t ResourceManager::update_node_load_info(ServerID server_id, const NodeLoadInfo& NodeLoadInfo)
{
    // nodeLoadInfoMap.nodeloadinfomap().insert(server_id, NodeLoadInfo);
    nodeLoadInfoMap[server_id] = NodeLoadInfo;
    return 0;
}
