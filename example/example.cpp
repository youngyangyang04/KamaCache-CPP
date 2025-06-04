#include <fmt/base.h>
#include <fmt/core.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <csignal>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "kcache/group.h"
#include "kcache/grpc_server.h"
#include "kcache/peer.h"

using namespace kcache;

DEFINE_int32(port, 8001, "节点端口");
DEFINE_string(node, "A", "节点标识符");

std::function<void(int)> handler_wrapper;

void HandleCtrlC(int signum) { handler_wrapper(signum); }

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[kcache] [%^%l%$] %v");

    std::string addr = "localhost:" + std::to_string(FLAGS_port);
    std::string service_name = "kcache";
    spdlog::info("[节点{}] 启动，地址: {}", FLAGS_node, addr);

    try {
        // 创建服务器选项
        ServerOptions opts;
        opts.etcd_endpoints = {"localhost:2379"};
        opts.dial_timeout = std::chrono::seconds(5);

        // 创建节点，同时注册到etcd
        auto node = std::make_unique<CacheGrpcServer>(addr, service_name);
        spdlog::info("[节点{}] 服务器创建成功", FLAGS_node);

        // 启动节点
        std::thread server_thread{[&] {
            spdlog::info("[节点{}] 开始启动服务...", FLAGS_node);
            try {
                node->Start();
            } catch (const std::exception& e) {
                spdlog::info("启动节点时发生异常: {}", e.what());
                std::exit(1);
            }
        }};

        // 注册 Ctrl+C 信号处理器用来优雅关闭服务
        handler_wrapper = [&](int signal) {
            if (signal == SIGINT) {
                spdlog::info("[节点{}] 收到Ctrl+C信号，正在关闭服务...", FLAGS_node);
                if (node) {
                    node->Stop();
                }
                spdlog::info("[节点{}] 服务已停止", FLAGS_node);
                // 不直接 exit，而是要等其他工作线程完成清理工作
            }
        };
        signal(SIGINT, HandleCtrlC);

        std::this_thread::sleep_for(std::chrono::seconds(5));  // 等待服务器启动

        // 创建缓存组
        auto& group = MakeCacheGroup("test", 2 << 20, [&](const std::string& key) -> ByteViewOptional {
            spdlog::info("[节点{}] 触发数据源加载: key={}", FLAGS_node, key);
            std::string value = fmt::format("节点{}的数据源值", FLAGS_node);
            return ByteView{value};
        });

        // 为cache group注册节点选择器
        group.RegisterPeerPicker(std::make_unique<PeerPicker>(addr, service_name));
        spdlog::info("[节点{}] 节点选择器注册成功", FLAGS_node);
        spdlog::info("[节点{}] 服务运行中，按Ctrl+C退出...", FLAGS_node);

        // ===================== test start =====================

        spdlog::info("接下来开启测试：", FLAGS_node);
        std::this_thread::sleep_for(std::chrono::seconds(10));
        spdlog::info("[节点{}] 测试开始：", FLAGS_node);

        {
            // 设置本节点的特定键值对
            std::string local_key = fmt::format("key_{}", FLAGS_node);
            std::string local_value = fmt::format("这是节点{}的数据", FLAGS_node);

            fmt::println("\n=== 节点{}：设置本地数据 ===", FLAGS_node);
            bool set_success = group.Set(local_key, ByteView{local_value});
            if (!set_success) {
                spdlog::info("设置本地数据失败");
                return 1;
            }
            spdlog::info("节点{}: 设置键 {} 成功", FLAGS_node, local_key);

            // 等待其他节点也完成设置
            spdlog::info("[节点{}] 等待其他节点准备就绪...", FLAGS_node);
            std::this_thread::sleep_for(std::chrono::seconds(5));

            // 测试获取本地数据
            fmt::println("\n=== 节点{}：获取本地数据 ===", FLAGS_node);
            spdlog::info("直接查询本地缓存...");

            auto local_val = group.Get(local_key);
            if (local_val) {
                spdlog::info("节点{}: 获取本地键 {} 成功: {}", FLAGS_node, local_key, local_val->ToString());
            } else {
                spdlog::info("节点{}: 获取本地键失败", FLAGS_node);
            }

            // 测试获取其他节点的数据
            std::vector<std::string> other_keys = {"key_A", "key_B", "key_C"};
            for (const auto& key : other_keys) {
                if (key == local_key) {
                    continue;
                }
                fmt::println("\n=== 节点{}：尝试获取远程数据 {} ===", FLAGS_node, key);
                spdlog::info("[节点{}] 开始查找键 {} 的远程节点", FLAGS_node, key);

                auto val = group.Get(key);
                if (val) {
                    spdlog::info("节点{}: 获取远程键 {} 成功: {}", FLAGS_node, key, val->ToString());
                } else {
                    spdlog::info("节点{}: 获取远程键失败", FLAGS_node);
                }
            }
        }
        // ===================== test end =====================

        // 等待服务器线程
        if (server_thread.joinable()) {
            server_thread.join();
        }

    } catch (const std::exception& e) {
        spdlog::info("[节点{}] 发生异常: {}", FLAGS_node, e.what());
        return 1;
    }

    return 0;
}