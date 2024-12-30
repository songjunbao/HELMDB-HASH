#pragma once

#include "server_struct.h"
#include <vector>
#include <mutex>
#include <stdexcept> // For std::runtime_error

using meta::ServerStatus;

class ServerList {
public:
    void add_server(const Server& server);
    void update_server_status(uint64_t id, meta::ServerStatus status);
    // Add more methods as needed
    Server get_server_by_id(uint64_t id);

private:
    std::vector<Server> servers;
    std::mutex mtx;
};
