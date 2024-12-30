#pragma once

#include <string>
#include <map>
#include <mutex>
#include "proto/meta.pb.h"

struct Server {
    uint64_t id;
    std::string hostname;
    uint32_t rpc_port;
    meta::InstanceKind instance_kind;
    meta::ServerStatus status;
    std::string version;
    std::string git_branch;
    std::string git_hash;
    std::map<std::string, std::string> attributes;

    template <typename Request>
    static Server from(const Request& req);
};

template <>
inline Server Server::from(const meta::RegisterServerRequest& req) {
    Server server;
    if (req.has_id()) {
        server.id = req.id();
    } else {
        server.id = 0;
    }
    server.hostname = req.hostname();
    server.rpc_port = req.rpc_port();
    server.instance_kind = req.instance_kind();
    server.status = req.status();
    server.version = req.version();
    server.git_branch = req.git_branch();
    server.git_hash = req.git_hash();
    for (const auto& attribute : req.attributes()) {
        server.attributes[attribute.first] = attribute.second;
    }
    return server;
}