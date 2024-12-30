#include "server_list.h"

void ServerList::add_server(const Server& server) {
    std::lock_guard<std::mutex> lock(mtx);

    // Check if server with same id already exists
    for (auto& srv : servers) {
        if (srv.id == server.id) {
            throw std::runtime_error("Server with same ID already exists.");
        }
    }

    servers.push_back(server);
}

void ServerList::update_server_status(uint64_t id, meta::ServerStatus status) {
    std::lock_guard<std::mutex> lock(mtx);

    // Find the server with the given id and update its status
    for (auto& srv : servers) {
        if (srv.id == id) {
            srv.status = status;
            return; // Exit the loop after updating status
        }
    }

    // If server with given id not found, throw an exception
    throw std::runtime_error("Server with specified ID not found.");
}

Server ServerList::get_server_by_id(uint64_t id) {
    std::lock_guard<std::mutex> lock(mtx);

    // Find the server with the given id and return it
    for (auto& srv : servers) {
        if (srv.id == id) {
            return srv;
        }
    }

    // If server with given id not found, throw an exception
    throw std::runtime_error("Server with specified ID not found.");
}