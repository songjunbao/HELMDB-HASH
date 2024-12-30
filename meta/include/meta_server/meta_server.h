#pragma once
#include "proto/meta.pb.h"
#include "cluster_manager.h"
#include "brpc/server.h"

using meta::MetaService;

class MetaServiceImpl : public MetaService {
public:
    virtual ~MetaServiceImpl();
    static MetaServiceImpl *get_instance()
    {
        static MetaServiceImpl _instance;
        return &_instance;
    }

    void RegisterServer(google::protobuf::RpcController *cntl_base, const meta::RegisterServerRequest *request,
                        meta::RegisterServerResponse *response, google::protobuf::Closure *done) override;
    void HeartBeat(google::protobuf::RpcController *cntl_base, const meta::Ping *request, meta::Pong *response,
                   google::protobuf::Closure *done) override;

    void GetServerStatus(google::protobuf::RpcController *cntl_base, const meta::GetServerStatusRequest *request,
                         meta::GetServerStatusResponse *response, google::protobuf::Closure *done) override;

    void SetServerStatus(google::protobuf::RpcController *cntl_base, const meta::SetServerStatusRequest *request,
                         meta::SetServerStatusResponse *response, google::protobuf::Closure *done) override;
};