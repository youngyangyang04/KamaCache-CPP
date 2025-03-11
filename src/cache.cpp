#include "cache.h"

#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "http.h"
#include "lru.h"

namespace kcache {

std::unordered_map<std::string, CacheGroup> cache_groups;
std::mutex mtx;

auto NewCacheGroup(const std::string& name, int64_t bytes, Getter getter) -> CacheGroup& {
    if (getter == nullptr) {
        std::cout << "no getter function!\n";
        std::terminate();
    }
    std::lock_guard lock{mtx};
    cache_groups[name] = std::move(CacheGroup{name, bytes, getter});
    return cache_groups[name];
}

auto GetCacheGroup(const std::string& name) -> CacheGroup* {
    std::lock_guard lock{mtx};
    if (!cache_groups.contains(name)) {
        return nullptr;
    }
    return &cache_groups[name];
}

auto CacheGroup::Get(const std::string& key) -> std::optional<ValueRef> {
    if (key == "") {
        return std::nullopt;
    }
    auto ret = cache_->Get(key);
    if (ret) {
        std::cout << std::format("^_^ cache hit, search [\"{}\"] from \"http://{}:{}\" \n", key, peers_->GetHost(),
                                 peers_->GetPort());
        return ret;
    }
    return Load(key);
}

void CacheGroup::RegisterPeers(std::unique_ptr<HTTPPool>&& peers) {
    if (peers_) {
        std::cout << "call RegisterPeers more than once!\n";
        std::terminate();
    }
    peers_ = std::move(peers);
    peers_->Start();
}

auto CacheGroup::Load(const std::string& key) -> std::optional<ValueRef> {
    if (peers_) {
        if (auto peer = peers_->GetPeer(key); peer) {
            if (auto ret = LoadFromPeer(peer, key); ret) {
                return ret;
            }
            std::cout << "fail to get from peer\n";
        }
    }
    return LoadFromLocal(key);
}

auto CacheGroup::LoadFromLocal(const std::string& key) -> std::optional<ValueRef> {
    auto ret = getter_(key);
    if (!ret) {
        return ret;
    }
    auto value = ret.value();
    cache_->Put(key, value);
    return value;
}

auto CacheGroup::LoadFromPeer(Peer* peer, const std::string& key) -> std::optional<ValueRef> {
    auto ret = peer->Get(name_, key);
    if (!ret) {
        return std::nullopt;
    }
    return std::make_shared<ByteValue>(ret.value());
}

}  // namespace kcache