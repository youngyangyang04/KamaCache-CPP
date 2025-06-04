#include <fmt/base.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <cassert>
#include <cstdio>
#include <etcd/Client.hpp>
#include <etcd/Response.hpp>
#include <etcd/Value.hpp>
#include <etcd/Watcher.hpp>
#include <memory>
#include <mutex>
#include <string>

#include "kcache/consistent_hash.h"
#include "kcache/peer.h"

namespace kcache {

PeerPicker::PeerPicker(const std::string& addr, const std::string& svc_name)
    : self_addr_(addr), service_name_(svc_name) {
    etcd_client_ = std::make_shared<etcd::Client>("http://127.0.0.1:2379");

    // 启动服务发现
    if (!StartServiceDiscovery()) {
        spdlog::error("Failed to start service discovery.");
        throw std::runtime_error("Failed to start service discovery.");
    }
}

PeerPicker::~PeerPicker() {
    is_stop_watch_ = true;
    etcd_watcher_->Cancel();
    if (etcd_watch_thread_.joinable()) {
        etcd_watch_thread_.join();
    }
}

bool PeerPicker::StartServiceDiscovery() {
    // 获取所有可用的节点
    if (!FetchAllServices()) {
        return false;
    }
    etcd_watch_thread_ = std::thread{[this] { this->WatchServiceChanges(); }};
    return true;
}

void PeerPicker::WatchServiceChanges() {
    std::string prefix_key = "/services/" + service_name_ + "/";
    etcd_watcher_ = std::make_unique<etcd::Watcher>(
        *etcd_client_, prefix_key, [this](etcd::Response resp) { HandleWatchEvents(resp); }, true);
    etcd_watcher_->Wait();
}

void PeerPicker::HandleWatchEvents(const etcd::Response& resp) {
    std::lock_guard lock{mtx_};
    if (!resp.is_ok()) {
        spdlog::error("Failed to watching etcd: {}", resp.error_message());
        return;
    }
    for (const auto& event : resp.events()) {
        std::string addr = ParseAddrFromKey(event.kv().as_string());
        if (addr.empty() || addr == self_addr_) {
            continue;
        }
        switch (event.event_type()) {
            case etcd::Event::EventType::PUT: {
                Set(addr);
                spdlog::debug("Service added at {}", addr);
                break;
            }
            case etcd::Event::EventType::DELETE_: {
                Remove(addr);
                spdlog::debug("Service removed at {}", addr);
                break;
            }
            default:
                spdlog::debug("Unknown event type: {}", static_cast<int>(event.event_type()));
                break;
        }
    }
}

auto PeerPicker::PickPeer(const std::string& key) -> Peer* {
    std::lock_guard lock{mtx_};
    auto peer_name = cons_hash_.Get(key);
    if (!peer_name.empty() && peer_name != self_addr_) {
        spdlog::debug("PickPeer get key [{}] from node [{}]", key, peer_name);
        return peers_[peer_name].get();
    }
    return nullptr;
}

bool PeerPicker::FetchAllServices() {
    std::string prefix_key = "/services/" + service_name_ + "/";
    etcd::Response resp = etcd_client_->ls(prefix_key).get();  // 列出指定前缀下的所有键​​

    if (!resp.is_ok()) {
        spdlog::error("Failed to get all services from etcd now: {}", resp.error_message());
        return false;
    }

    // 每个 PeerPicker 都维护一个一致性哈希环
    // 节点信息通过 etcd 同步：每个节点将自己的地址注册到 etcd，其他节点通过监听 etcd 来发现新节点或节点下线
    // 哈希环本地更新：当发现节点变化时，每个 PeerPicker 会在本地更新自己的一致性哈希环
    std::unique_lock lock{mtx_};
    for (const auto& key : resp.keys()) {
        std::string addr = ParseAddrFromKey(key);
        if (!addr.empty() && addr != self_addr_) {
            Set(addr);
            spdlog::debug("Discovered service at {}", addr);
        }
    }
    return true;
}

void PeerPicker::Set(const std::string& addr) {
    auto client = std::make_shared<Peer>(addr, service_name_, etcd_client_);
    cons_hash_.Add({addr});
    peers_[addr] = client;
}

void PeerPicker::Remove(const std::string& addr) {
    cons_hash_.Remove(addr);
    peers_.erase(addr);
}

auto PeerPicker::ParseAddrFromKey(const std::string& key) -> std::string {
    std::string prefix = "/services/" + service_name_ + "/";
    if (key.rfind(prefix, 0) == 0) {  // 检查是否以 prefix 开头
        return key.substr(prefix.length());
    }
    return "";
}

}  // namespace kcache