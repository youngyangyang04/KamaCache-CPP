#include <gflags/gflags.h>
#include <httplib.h>

#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "cache.h"
#include "http.h"

DEFINE_int32(port, 8001, "kcache server's port");
DEFINE_bool(api, false, "start a api server or not");

std::unordered_map<std::string, std::string> db = {
    {"Tom", "400"}, {"Kerolt", "370"},  {"Jack", "296"},  {"Alice", "320"},
    {"Bob", "280"}, {"Charlie", "410"}, {"Diana", "390"}, {"Eve", "310"},
};

std::unordered_map<int, std::pair<std::string, int>> addrs_map = {
    {8001, {"localhost", 8001}},
    {8002, {"localhost", 8002}},
    {8003, {"localhost", 8003}},
};

void StartCacheServer(kcache::CacheGroup& cache, const std::string& host, int port,
                      const std::vector<std::string>& addrs) {
    std::cout << std::format("cache server running at http://{}:{}\n", host, port);
    auto peers = std::make_unique<kcache::HTTPPool>(host, port);
    peers->SetPeers(addrs);
    cache.RegisterPeers(std::move(peers));
}

void StartApiServer(kcache::CacheGroup& cache, const std::string& host, int port) {
    std::cout << std::format("api server running at http://{}:{}\n", host, port);
    httplib::Server api_server;
    api_server.Get("/api", [&](const httplib::Request& request, httplib::Response& response) {
        // http://host:port/api?key=val
        if (request.has_param("key")) {
            auto key = request.get_param_value("key");
            auto val = cache.Get(key);
            if (!val) {
                response.set_content(std::format("not found key [\"{}\"]", key), "text/plain");
                return;
            }
            response.set_header("Content-Type", "application/octet-stream");
            response.set_content(val.value()->ToString(), "text/plain");
        }
    });
    api_server.listen(host, port);
}

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    auto& cache =
        kcache::NewCacheGroup("score", 2 << 10, [&](const std::string& key) -> std::optional<kcache::ValueRef> {
            if (db.contains(key)) {
                std::cout << std::format(">_< search [{}] from db\n", key);
                return std::make_shared<kcache::ByteValue>(db[key]);
            }
            std::cout << std::format(">_< Uh oh, there is not found [{}]\n", key);
            return std::nullopt;
        });

    std::thread api_server;
    if (FLAGS_api) {
        // 如果启用了 api server，那么所有请求都会从启用了 api server 的 cache server 去向其他 peer 节点请求
        api_server = std::thread{StartApiServer, std::ref(cache), "localhost", 9999};
    }

    std::vector<std::string> addrs;
    for (const auto& [port, pair] : addrs_map) {
        addrs.emplace_back(std::format("http://{}:{}", pair.first, pair.second));
    }

    StartCacheServer(cache, "localhost", FLAGS_port, addrs);

    api_server.join();
}
