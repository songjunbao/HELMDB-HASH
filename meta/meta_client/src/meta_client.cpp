#include "meta_client.h"

MetaClient::MetaClient(const std::string &server_address)
{
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.timeout_ms = 1000;
    options.max_retry = 3;
    
    if (channel_.Init("127.0.0.1:8000", &options) != 0) {
        std::cerr << "Fail to initialize channel" << std::endl;
    }
}

MetaClient::~MetaClient() {}

// 实现 getter 和 setter
uint64_t MetaClient::get_id() const {
    return id;
}

uint64_t MetaClient::RegisterServer()
{
    MetaService_Stub stub(&channel_);
    RegisterServerRequest request;
    RegisterServerResponse response;
    brpc::Controller cntl;

    request.set_hostname("127.0.0.1");
    request.set_rpc_port(8010);
    request.set_instance_kind(meta::InstanceKind::TEST);
    request.set_status(meta::ServerStatus::STARTING);
    request.set_version("v1.0.0");
    request.set_git_branch("main");
    request.set_git_hash("test_hash");
    stub.RegisterServer(&cntl, &request, &response, NULL);

    if (!cntl.Failed()) {
        this->id = response.id();
        return response.id();
    } else {
        return 0;
    }
}

ServerStatus MetaClient::SetServerStatus(ServerStatus status){
    MetaService_Stub stub(&channel_);
    SetServerStatusRequest request;
    SetServerStatusResponse response;
    brpc::Controller cntl;

    request.set_id(this->id);
    request.set_status(status);
    stub.SetServerStatus(&cntl, &request, &response, NULL);

    if (!cntl.Failed()) {
        return response.status();
    } else {
        throw std::runtime_error("RPC failed");
    }
}

ServerStatus MetaClient::GetServerStatus(uint64_t server_id) {
    MetaService_Stub stub(&channel_);
    GetServerStatusRequest request;
    GetServerStatusResponse response;
    brpc::Controller cntl;

    request.set_id(server_id);
    stub.GetServerStatus(&cntl, &request, &response, NULL);

    if (!cntl.Failed()) {
        return response.status();
    } else {
        throw std::runtime_error("RPC failed");
    }

}

uint64_t MetaClient::UpdateNodeLoadInfo(NodeLoadInfo node_load_info)
{
    Task2Service_Stub stub(&channel_);
    UpdateNodeLoadInfoRequest request;
    UpdateNodeLoadInfoResponse response;
    brpc::Controller cntl;

    request.set_id(this->id);
    request.set_timestamp(time(NULL));
    *request.mutable_nodeloadinfo() = node_load_info;
    stub.UpdateNodeLoadInfo(&cntl, &request, &response, NULL);

    if (!cntl.Failed()) {
        return response.id();
    } else {
        return 0;
    }
}