// meta_client.h
#pragma once

#include <string>
#include "meta.pb.h"
#include "task_2.pb.h"
#include <brpc/channel.h>

using meta::MetaService_Stub;
using meta::RegisterServerRequest;
using meta::RegisterServerResponse;
using meta::GetServerStatusRequest;
using meta::GetServerStatusResponse;
using meta::SetServerStatusRequest;
using meta::SetServerStatusResponse;
using meta::ServerStatus;
using meta::NodeLoadInfo;

// for task2
using meta::Task2Service_Stub;
using meta::NodeLoadInfo;
using meta::UpdateNodeLoadInfoRequest;
using meta::UpdateNodeLoadInfoResponse;

class MetaClient {
public:
    MetaClient(const std::string &server_address);
    ~MetaClient();

    uint64_t RegisterServer();
    ServerStatus SetServerStatus(ServerStatus status);
    ServerStatus GetServerStatus(uint64_t server_id);

    uint64_t UpdateNodeLoadInfo(NodeLoadInfo node_load_info);

    // getter 和 setter
    uint64_t get_id() const;

private:
    uint64_t id;
    brpc::Channel channel_;
};
