#include "task2_service_impl.h"

Task2ServiceImpl::~Task2ServiceImpl()
{}

void Task2ServiceImpl::UpdateNodeLoadInfo(google::protobuf::RpcController *cntl_base,
                                         const meta::UpdateNodeLoadInfoRequest *request, meta::UpdateNodeLoadInfoResponse *response,
                                         google::protobuf::Closure *done)
{
    brpc::ClosureGuard done_guard(done);
    auto server_id = request->id();
    auto node_load_info = request->nodeloadinfo();
    ResourceManager::instance().update_node_load_info(server_id, node_load_info);
    response->set_id(server_id);
}