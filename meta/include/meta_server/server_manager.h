#pragma once

#include "server_list.h"
#include <atomic>
#include <mutex>

class ServerManager {
public:
    ServerManager();
    uint64_t add_server(const Server& server);

    Server get_server_by_id(uint64_t server_id);

    ServerStatus get_server_status(uint64_t server_id);

    bool set_server_status(uint64_t server_id, ServerStatus status);

private:
    ServerList server_list;
    std::atomic<uint64_t> server_id_generator;
    std::mutex mtx;
};
