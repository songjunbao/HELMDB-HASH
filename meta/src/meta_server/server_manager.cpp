#include "server_manager.h"

ServerManager::ServerManager() : server_id_generator(1) {}

uint64_t ServerManager::add_server(const Server& server) {
    std::lock_guard<std::mutex> lock(mtx);
    if (server.id != 0) {
        server_list.add_server(server);
        return server.id;
    } else {
        Server new_server = server;
        new_server.id = server_id_generator++;
        server_list.add_server(new_server);
        return new_server.id;
    }
}

Server ServerManager::get_server_by_id(uint64_t server_id) {
    return server_list.get_server_by_id(server_id);
}

ServerStatus ServerManager::get_server_status(uint64_t server_id) {
    return server_list.get_server_by_id(server_id).status;
}

bool ServerManager::set_server_status(uint64_t server_id, ServerStatus status) {
    try {
        server_list.update_server_status(server_id, status);
        return true;
    } catch (std::runtime_error& e) {
        return false;
    }
}
