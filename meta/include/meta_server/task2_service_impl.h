#pragma once
#include "proto/task_2.pb.h"
#include "resource_manager.h"
#include "brpc/server.h"

using meta::Task2Service;

class Task2ServiceImpl : public Task2Service {
public:
    virtual ~Task2ServiceImpl();
    static Task2ServiceImpl *get_instance()
    {
        static Task2ServiceImpl _instance;
        return &_instance;
    }

    void UpdateNodeLoadInfo(google::protobuf::RpcController *cntl_base, const meta::UpdateNodeLoadInfoRequest *request,
                            meta::UpdateNodeLoadInfoResponse *response, google::protobuf::Closure *done) override;
};
