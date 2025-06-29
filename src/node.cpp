#include <chrono>
#include <csignal>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <fmt/base.h>
#include <fmt/core.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include "kcache/cache.h"
#include "kcache/group.h"
#include "kcache/grpc_server.h"
#include "kcache/peer.h"

using namespace kcache;

DEFINE_int32(port, 8001, "节点端口");
DEFINE_string(node, "A", "节点标识符");

// 模拟数据库
std::unordered_map<std::string, std::string> db = {
    {"Tom", "400"}, {"Kerolt", "370"},  {"Jack", "296"},  {"Alice", "320"},
    {"Bob", "280"}, {"Charlie", "410"}, {"Diana", "390"}, {"Eve", "310"},
};

std::function<void(int)> handler_wrapper;
void HandleCtrlC(int signum) { handler_wrapper(signum); }

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[knode][%^%l%$] %v");

    std::string addr = "localhost:" + std::to_string(FLAGS_port);
    std::string service_name = "kcache";
    spdlog::info("[node{}] start at: {}", FLAGS_node, addr);

    try {
        // 创建服务器选项
        ServerOptions opts;
        opts.etcd_endpoints = {"localhost:2379"};
        opts.dial_timeout = std::chrono::seconds(5);

        // 创建节点，同时注册到etcd
        auto node = std::make_unique<CacheGrpcServer>(addr, service_name);
        spdlog::info("[node{}] server created successfully", FLAGS_node);

        // 启动节点
        std::thread server_thread{[&] {
            spdlog::info("[node{}] starting service...", FLAGS_node);
            try {
                node->Start();
            } catch (const std::exception& e) {
                spdlog::info("[node{}] failed to start service: {}", FLAGS_node, e.what());
                std::exit(1);
            }
        }};

        // 注册 Ctrl+C 信号处理器用来优雅关闭服务
        handler_wrapper = [&](int signal) {
            if (signal == SIGINT) {
                spdlog::info("[node{}] received Ctrl+C signal, shutting down service...", FLAGS_node);
                if (node) {
                    node->Stop();
                }
                spdlog::info("[node{}] service stopped", FLAGS_node);
                // 不直接 exit，而是要等其他工作线程完成清理工作
            }
        };
        signal(SIGINT, HandleCtrlC);

        std::this_thread::sleep_for(std::chrono::seconds(5));  // 等待服务器启动

        // 创建缓存组
        auto& group = MakeCacheGroup("test", 2 << 20, [&](const std::string& key) -> ByteViewOptional {
            if (db.find(key) != db.end()) {
                spdlog::info(">_< search [{}] from db\n", key);
                return ByteView{db[key]};
            }
            spdlog::info(">_< Uh oh, there is not found [{}]\n", key);
            return std::nullopt;
        });

        // 为cache group注册节点选择器
        group.RegisterPeerPicker(std::make_unique<PeerPicker>(addr, service_name));
        spdlog::info("[node{}] peer picker registered successfully", FLAGS_node);
        spdlog::info("[node{}] service running, press Ctrl+C to exit...", FLAGS_node);

        // 等待服务器线程
        if (server_thread.joinable()) {
            server_thread.join();
        }

    } catch (const std::exception& e) {
        spdlog::error("[node{}] exception occurred: {}", FLAGS_node, e.what());
        std::exit(1);
    }

    return 0;
}