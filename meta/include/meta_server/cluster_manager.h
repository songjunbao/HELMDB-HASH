#pragma once

#include "server_manager.h"
#include <memory>
#include "proto/meta.pb.h"
#include "meta_map.h"
#include "meta_common.h"

using meta::MetaMap;
using meta::ServerStatus;

class ClusterManager {
public:
    static ClusterManager& instance();
    uint64_t register_server(const Server& server);
    ServerStatus get_server_status(uint64_t server_id);
    bool set_server_status(uint64_t server_id, ServerStatus status);

private:
    ClusterManager();
    MetaMap<meta::InstanceKind, std::shared_ptr<ServerManager>> server_managers;
};
