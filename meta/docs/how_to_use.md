# Meta使用示例
这边以课题二的资源监控需求为例，说明使用meta的流程
## 编写protobuf
首先在`meta/proto`文件夹下创建`task2.proto`文件，protobuf的代码规范和教程可以参考[protobuf.md](./protobuf.md)和[https://protobuf.dev](https://protobuf.dev)

添加必要的文件头
```protobuf
syntax="proto3";

package meta;
```
添加服务数据结构，*注：应为每个课题定义一个RPC服务*
```protobuf
service Task2Service {

}
```

然后添加数据结构的定义
```protobuf
// 节点负载信息
message NodeLoadInfo {
    uint8 cpuUsage=1; 
    uint8 mem_usage=2;
    uint8 pmem_usage=3;
    uint8 ssd_usage=4;
}
```

添加rpc请求和响应的数据结构
```protobuf
message UpdateNodeLoadInfoRequest {
    uint64 id = 1;
    uint64 timestamp= 2;
    NodeLoadInfo nodeLoadInfo = 3;
}

message UpdateNodeLoadInfoResponse {
    uint64 id = 1; 
    string message = 2;
}
```

添加RPC方法定义到service中
```protobuf
service Task2Service {
    rpc UpdateNodeLoadInfo(UpdateNodeLoadInfoRequest) returns (UpdateNodeLoadInfoResponse);
}
```

总的`task_2.proto`如下
```protobuf
syntax="proto3";

package meta;

option cc_generic_services = true;

// The task2 service definition
service Task2Service {
    rpc UpdateNodeLoadInfo(UpdateNodeLoadInfoRequest) returns (UpdateNodeLoadInfoResponse);
}

// 节点负载信息
message NodeLoadInfo {
    uint8 cpuUsage=1; 
    uint8 mem_usage=2;
    uint8 pmem_usage=3;
    uint8 ssd_usage=4;
}

message UpdateNodeLoadInfoRequest {
    uint64 id = 1;
    uint64 timestamp= 2;
    NodeLoadInfo nodeLoadInfo = 3;
}

message UpdateNodeLoadInfoResponse {
    uint64 id = 1; 
    string message = 2;
}
```

## 编译protobuf
重新编译`meta`，`meta/proto`文件夹下将会出现同名的C++定义文件`task_2.pb.cc`和`task_2.pb.h`

## 编写server端逻辑
在`meta/include/meta_server`下创建`resource_manager.h`，内容如下
```cpp
#pragma once

#include <map>
#include <memory>
#include "proto/task_2.pb.h"
#include "common.h"

class ResourceManager {
public:
    static ResourceManager& instance();
    uint64_t register_server(const Server& server);

private:
    ResourceManager();
    std::map<ServerID, meta::NodeLoadInfo> nodeLoadInfo;
};
```

在`meta/src/meta_server`下编写`resource_manager.cpp`
```cpp
#include "resource_manager.h"

ResourceManager& ResourceManager::instance() {
    static ResourceManager instance;
    return instance;
}

ResourceManager::ResourceManager() {
}

uint64_t ResourceManager::update_node_load_info(ServerID server_id, const NodeLoadInfo& NodeLoadInfo)
{
    nodeLoadInfoMap[server_id] = NodeLoadInfo;
    return 0;
}

```

在`meta/include`下定义服务实现`task2_service_impl.h`
```cpp
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
```

在`meta/src/meta_server`下编写服务实现`task2_service_impl.cpp`

```cpp
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
```

在`meta/src/meta_server/main.cpp`中添加服务
```cpp
    if (server.AddService(&task2_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add service";
        return -1;
    }
```

## 编写client逻辑
`meta/meta_client/include/meta_client.h`中
```cpp
#include "task_2.pb.h" // 引入头文件

// 使用proto request和response定义
using meta::UpdateNodeLoadInfoRequest; 
using meta::UpdateNodeLoadInfoResponse;

class MetaClient {
public:
    ...
    uint64_t UpdateNodeLoadInfo(); // 添加方法定义
    ...
}
```

在`meta/meta_client/src/meta_client.cpp`中填写实现
```cpp
uint64_t MetaClient::UpdateNodeLoadInfo()
{
    Task2Service_Stub stub(&channel_);
    NodeLoadInfo node_load_info;
    UpdateNodeLoadInfoRequest request;
    UpdateNodeLoadInfoResponse response;
    brpc::Controller cntl;

    node_load_info.set_cpuusage(0.5);
    node_load_info.set_mem_usage(0.5);
    node_load_info.set_pmem_usage(0.5);
    node_load_info.set_ssd_usage(0.5);

    request.set_id(1);
    request.set_timestamp(time(NULL));
    request.set_allocated_nodeloadinfo(&node_load_info);
    stub.UpdateNodeLoadInfo(&cntl, &request, &response, NULL);

    if (!cntl.Failed()) {
        return response.id();
    } else {
        return 0;
    }
}
```

## 编译
检查完毕后重新编译meta

## 测试
在`meta_example/main.cpp`的main函数中添加
```cpp
    id = client.UpdateNodeLoadInfo();
    LOG(INFO) << "Server id: " << id;
```



