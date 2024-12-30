#include "meta_server.h"
#include "task2_service_impl.h"
#include <brpc/server.h>
#include <unordered_map>
#include <mutex>

int main(int argc, char* argv[]) {
    brpc::Server server;

    MetaServiceImpl meta_service_impl;
    Task2ServiceImpl task2_service_impl;
    if (server.AddService(&meta_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add service";
        return -1;
    }
    if (server.AddService(&task2_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add service";
        return -1;
    }

    brpc::ServerOptions options;
    if (server.Start(8000, &options) != 0) {
        LOG(ERROR) << "Failed to start server";
        return -1;
    }

    server.RunUntilAskedToQuit();
    return 0; 
}