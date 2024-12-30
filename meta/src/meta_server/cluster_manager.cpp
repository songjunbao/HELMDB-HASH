#include "cluster_manager.h"

ClusterManager& ClusterManager::instance() {
    static ClusterManager instance;
    return instance;
}

ClusterManager::ClusterManager() {
    // Initialize ServerManagers for each InstanceKind
    server_managers[meta::InstanceKind::TEST] = std::make_shared<ServerManager>();
}

uint64_t ClusterManager::register_server(const Server& server) {
    auto it = server_managers.find(server.instance_kind);
    if (it != server_managers.end()) {
        return it->second->add_server(server);
    }
    // Handle error or add logic for other InstanceKinds
    return 0;
}

ServerStatus ClusterManager::get_server_status(uint64_t server_id)
{
    for (auto& [instance_kind, server_manager] : server_managers) {
        auto status = server_manager->get_server_status(server_id);
        if (status != ServerStatus::UNKNOWN) {
            return status;
        }
    }
};

bool ClusterManager::set_server_status(uint64_t server_id, ServerStatus status)
{
    for (auto& [instance_kind, server_manager] : server_managers) {
        if (server_manager->set_server_status(server_id, status)) {
            return true;
        }
    }
    return false;
}
