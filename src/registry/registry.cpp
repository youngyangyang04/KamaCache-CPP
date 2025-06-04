#include "kcache/registry.h"

#include <arpa/inet.h>
#include <fmt/core.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <chrono>
#include <etcd/KeepAlive.hpp>
#include <etcd/v3/Transaction.hpp>

namespace kcache {

bool EtcdRegistry::Register(const std::string& svc_name, std::string addr) {
    std::string local_ip = GetLocalIP();
    if (local_ip.empty()) {
        spdlog::error("Failed to get local IP");
        return false;
    }
    if (!addr.empty() && addr[0] == ':') {
        addr = local_ip + addr;
    }
    key_ = "/services/" + svc_name + "/" + addr;

    // 创建租约
    auto lease_resp = etcd_client_->leasegrant(10).get();
    if (!lease_resp.is_ok()) {
        spdlog::error("Failed to create lease: {}", lease_resp.error_message());
        return false;
    }
    lease_id_ = lease_resp.value().lease();

    // 注册服务
    auto is_ok = etcd_client_->put(key_, addr, lease_id_).get();
    if (!is_ok.is_ok()) {
        spdlog::error("Failed to register [{}] to etcd: {}", key_, is_ok.error_message());
        return false;
    }

    // 启动续约线程
    keepalive_thread_ = std::thread{[this] { this->KeepAliveLoop(); }};
    spdlog::info("Etcd Service registered: {}", key_);
    return true;
}

void EtcdRegistry::Unregister() {
    is_stop_ = true;
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }
    if (lease_id_ > 0) {
        auto is_ok = etcd_client_->leaserevoke(lease_id_).wait();
        if (!is_ok) {
            throw std::runtime_error(fmt::format("[kcache] Failed to revoke lease: {}", lease_id_));
        } else {
            spdlog::info("Lease {} revoked successfully", lease_id_);
        }
    }
    spdlog::info("Service unregistered: {}", key_);
}

auto EtcdRegistry::GetLocalIP() -> std::string {
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        return "";
    }
    std::string ip;
    for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN];
        void* addr_ptr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
        inet_ntop(AF_INET, addr_ptr, buf, INET_ADDRSTRLEN);
        std::string candidate{buf};
        if (candidate != "127.0.0.1") {
            ip = candidate;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}

void EtcdRegistry::KeepAliveLoop() {
    while (!is_stop_) {
        etcd::KeepAlive keepalive{*etcd_client_, 10, lease_id_};
        try {
            // fmt::println("[kcache] Lease {} keepalive successful", lease_id_);
            keepalive.Check();
        } catch (const std::exception& e) {
            // retry_count++;
            spdlog::error("Keepalive exception: {}", e.what());
            keepalive.Cancel();
        } catch (...) {
            spdlog::error("Keepalive unknown exception");
            keepalive.Cancel();
        }

        // 等待间隔（租约时间的1/3）
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    spdlog::debug("KeepAlive loop exited for lease {}", lease_id_);
}

}  // namespace kcache