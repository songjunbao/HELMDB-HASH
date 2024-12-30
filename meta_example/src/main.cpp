#include <iostream>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "meta.pb.h"
#include <brpc/channel.h>
#include "meta_client.h"

using meta::NodeLoadInfo;

DEFINE_string(input, "", "Input file");
DEFINE_int32(num_threads, 1, "Number of threads");


int main(int argc, char* argv[]) {
    FLAGS_logtostderr = true;
    google::InitGoogleLogging(argv[0]);

    // 定义日志目录
    FLAGS_log_dir = "./logs";

    meta::GetServerStatusRequest request;
    brpc::Channel channel;

    MetaClient client("http://127.0.0.1:8000");
    auto id = client.RegisterServer();
    LOG(INFO) << "Server id: " << id;

    auto status = client.GetServerStatus(client.get_id());
    client.GetServerStatus(client.get_id());

    LOG(INFO) << "Server status: " << status;

    client.SetServerStatus(ServerStatus::RUNNING);
    status = client.GetServerStatus(client.get_id());
    sleep(1); // wait for server to update status
    LOG(INFO) << "Server status after setting: " << status;

    NodeLoadInfo node_load_info;
    node_load_info.set_cpuusage(0.5);
    node_load_info.set_mem_usage(0.5);
    node_load_info.set_pmem_usage(0.5);
    node_load_info.set_ssd_usage(0.5);

    id = client.UpdateNodeLoadInfo(node_load_info);
    LOG(INFO) << "Server id: " << id;

    google::ShutdownGoogleLogging();
    
    return 0;
}