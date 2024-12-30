#include "meta_server.h"

MetaServiceImpl::~MetaServiceImpl()
{}
void MetaServiceImpl::RegisterServer(google::protobuf::RpcController *cntl_base,
                                     const meta::RegisterServerRequest *request, meta::RegisterServerResponse *response,
                                     google::protobuf::Closure *done)
{
    brpc::ClosureGuard done_guard(done);
    auto server = Server::from(*request);
    uint64_t server_id = ClusterManager::instance().register_server(server);
    response->set_id(server_id);
}

void MetaServiceImpl::HeartBeat(google::protobuf::RpcController *cntl_base, const meta::Ping *request,
                                meta::Pong *response, google::protobuf::Closure *done)
{
    brpc::ClosureGuard done_guard(done);
    response->set_timestamp(time(NULL));
}

void MetaServiceImpl::GetServerStatus(google::protobuf::RpcController *cntl_base,
                                      const meta::GetServerStatusRequest *request, meta::GetServerStatusResponse *response,
                                      google::protobuf::Closure *done)
{
    brpc::ClosureGuard done_guard(done);
    auto server_id = request->id();
    auto server_status = ClusterManager::instance().get_server_status(server_id);
    response->set_status(server_status);
}

void MetaServiceImpl::SetServerStatus(google::protobuf::RpcController *cntl_base,
                                      const meta::SetServerStatusRequest *request, meta::SetServerStatusResponse *response,
                                      google::protobuf::Closure *done)
{
    brpc::ClosureGuard done_guard(done);
    auto server_id = request->id();
    auto server_status = request->status();
    auto old_status = ClusterManager::instance().get_server_status(server_id);
    ClusterManager::instance().set_server_status(server_id, server_status);
    if (old_status == meta::ServerStatus::STOPPED && server_status == meta::ServerStatus::STARTING) {
        // 重启服务
    }
    response->set_status(server_status);
}