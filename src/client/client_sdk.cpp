#include "kcache/client.h"

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "kcache.grpc.pb.h"

namespace kcache {

KCacheClient::KCacheClient(const std::string& etcd_endpoints, const std::string& service_name)
    : service_name_(service_name) {
    etcd_client_ = std::make_shared<etcd::Client>(etcd_endpoints);
    StartServiceDiscovery();
}

KCacheClient::~KCacheClient() {
    if (etcd_watcher_) {
        etcd_watcher_->Cancel();
    }
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
}

auto KCacheClient::Get(const std::string& group, const std::string& key) -> std::optional<std::string> {
    auto target_addr = GetCacheNode(key);
    if (target_addr.empty()) {
        spdlog::warn("No cache service available for key: {}", key);
        return std::nullopt;
    }

    auto channel = grpc::CreateChannel(target_addr, grpc::InsecureChannelCredentials());
    auto client = pb::KCache::NewStub(channel);
    if (!client) {
        spdlog::error("Failed to create gRPC stub for node: {}", target_addr);
        return std::nullopt;
    }

    pb::Request request;
    request.set_group(group);
    request.set_key(key);

    pb::GetResponse response;
    grpc::ClientContext context;

    auto status = client->Get(&context, request, &response);
    if (status.ok()) {
        return response.value();
    } else {
        if (status.error_code() != grpc::StatusCode::NOT_FOUND) {
            spdlog::warn("Get failed on node {}: {} ({})", target_addr, status.error_message(),
                         static_cast<int>(status.error_code()));
        }
        return std::nullopt;
    }
}

bool KCacheClient::Set(const std::string& group, const std::string& key, const std::string& value) {
    auto target_addr = GetCacheNode(key);
    if (target_addr.empty()) {
        spdlog::warn("No cache service available for Set");
        return false;
    }

    auto channel = grpc::CreateChannel(target_addr, grpc::InsecureChannelCredentials());
    auto client = pb::KCache::NewStub(channel);
    if (!client) {
        spdlog::error("Failed to create gRPC stub for node: {}", target_addr);
        return false;
    }

    pb::Request request;
    request.set_group(group);
    request.set_key(key);
    request.set_value(value);

    pb::SetResponse response;
    grpc::ClientContext context;
    auto status = client->Set(&context, request, &response);

    if (!(status.ok() && response.value())) {
        spdlog::error("Failed to set value on node {}: {}", target_addr, status.error_message());
        return false;
    }

    // 广播其他节点发送缓存失效通知
    bool all_success = true;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (const auto& addr : cache_nodes_) {
            if (addr != target_addr) {
                auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
                auto client = pb::KCache::NewStub(channel);
                pb::InvalidateResponse response;
                grpc::ClientContext ctx;

                auto status = client->Invalidate(&ctx, request, &response);
                if (!(status.ok() && response.value())) {
                    all_success = false;
                    spdlog::warn("Failed to Invalidate key on node {}", addr);
                }
            }
        }
    }

    return all_success;
}

bool KCacheClient::Delete(const std::string& group, const std::string& key) {
    pb::Request request;
    request.set_group(group);
    request.set_key(key);

    bool all_success = true;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        if (cache_nodes_.empty()) {
            spdlog::warn("No cache service available for Delete");
            return false;
        }

        for (const auto& addr : cache_nodes_) {
            auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
            auto client = pb::KCache::NewStub(channel);
            grpc::ClientContext ctx;
            pb::DeleteResponse response;

            auto status = client->Delete(&ctx, request, &response);
            if (!(status.ok() && response.value())) {
                all_success = false;
                spdlog::warn("Failed to delete key on node {}", addr);
            }
        }
    }
    return all_success;
}

bool KCacheClient::StartServiceDiscovery() {
    if (!FetchAllServices()) {
        return false;
    }
    discovery_thread_ = std::thread{[this] {
        std::string prefix = "/services/" + service_name_ + "/";
        spdlog::debug("Starting etcd watcher for prefix: {}", prefix);
        etcd_watcher_ = std::make_unique<etcd::Watcher>(
            *etcd_client_, prefix, [this](etcd::Response resp) { HandleWatchEvents(resp); }, true);
        etcd_watcher_->Wait();
    }};
    return true;
}

void KCacheClient::HandleWatchEvents(const etcd::Response& resp) {
    std::lock_guard<std::mutex> lock{nodes_mutex_};
    if (!resp.is_ok()) {
        spdlog::error("Failed to watching etcd: {}", resp.error_message());
        return;
    }

    for (const auto& event : resp.events()) {
        std::string key = event.kv().key();
        std::string addr = ParseAddrFromKey(key);
        if (addr.empty()) {
            continue;
        }
        switch (event.event_type()) {
            case etcd::Event::EventType::PUT: {
                if (cache_nodes_.find(addr) == cache_nodes_.end()) {
                    cache_nodes_.insert(addr);
                    consistent_hash_.Add({addr});
                }
                spdlog::debug("Service added: {} (key: {})", addr, key);
                break;
            }
            case etcd::Event::EventType::DELETE_: {
                if (cache_nodes_.find(addr) != cache_nodes_.end()) {
                    cache_nodes_.erase(addr);
                    consistent_hash_.Remove(addr);
                    spdlog::debug("Service removed: {} (key: {})", addr, key);
                }
                break;
            }
            default:
                spdlog::debug("Unknown event type: {} for key: {}", static_cast<int>(event.event_type()), key);
                break;
        }
    }
}

bool KCacheClient::FetchAllServices() {
    std::string prefix_key = "/services/" + service_name_ + "/";
    etcd::Response resp = etcd_client_->ls(prefix_key).get();

    if (!resp.is_ok()) {
        spdlog::error("Failed to get all services from etcd now: {}", resp.error_message());
        return false;
    }
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    for (const auto& key : resp.keys()) {
        std::string addr = ParseAddrFromKey(key);
        if (!addr.empty()) {
            cache_nodes_.insert(addr);
            consistent_hash_.Add({addr});
            spdlog::debug("Discovered service at {}", addr);
        }
    }
    return true;
}

auto KCacheClient::ParseAddrFromKey(const std::string& key) -> std::string {
    std::string prefix = "/services/" + service_name_ + "/";
    if (key.rfind(prefix, 0) == 0) {
        return key.substr(prefix.length());
    }
    return "";
}

auto KCacheClient::GetCacheNode(const std::string& key) -> std::string {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    if (cache_nodes_.empty()) {
        return "";
    }

    std::string target_addr = consistent_hash_.Get(key);
    if (target_addr.empty()) {
        target_addr = *cache_nodes_.begin();
    }

    spdlog::debug("Routing key '{}' to node '{}'", key, target_addr);
    return target_addr;
}

}  // namespace kcache
