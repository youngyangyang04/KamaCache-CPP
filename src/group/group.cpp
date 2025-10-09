#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <fmt/base.h>
#include <spdlog/spdlog.h>

#include "kcache/cache.h"
#include "kcache/group.h"
#include "kcache/peer.h"

namespace kcache {

std::unordered_map<std::string, CacheGroup> cache_groups;
std::mutex mtx;

auto MakeCacheGroup(const std::string& name, int64_t bytes, DataGetter getter) -> CacheGroup& {
    if (getter == nullptr) {
        spdlog::critical("no getter function!");
        std::exit(1);
    }
    std::lock_guard lock{mtx};
    cache_groups[name] = std::move(CacheGroup{name, bytes, getter});
    return cache_groups[name];
}

auto GetCacheGroup(const std::string& name) -> CacheGroup* {
    std::lock_guard lock{mtx};
    if (cache_groups.find(name) == cache_groups.end()) {
        return nullptr;
    }
    return &cache_groups[name];
}

auto CacheGroup::Get(const std::string& key) -> ByteViewOptional {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return std::nullopt;
    }

    if (key == "") {
        spdlog::warn("The key [{}] is empty, you can't get a empty key from cache group", key);
        return std::nullopt;
    }

    // 先从本地缓存中获取
    auto ret = cache_->Get(key);
    if (ret) {
        ++status_.local_hits;  // 本地命中缓存次数+1
        return ret;
    }

    ++status_.local_misses;  // 本地未命中缓存次数+1
    return Load(key);
}

bool CacheGroup::Set(const std::string& key, ByteView b, bool is_from_peer) {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return false;
    }
    if (key.empty()) {
        spdlog::warn("The key [{}] is empty, you can't set it into cache group", key);
        return false;
    }
    cache_->Set(key, b);
    // 必须是本身这个节点设置的值，才会同步到其他节点
    if (!is_from_peer && peer_picker_) {
        SyncToPeers(key, SyncFlag::SET, b);
    }
    return true;
}

bool CacheGroup::Delete(const std::string& key, bool is_from_peer) {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return false;
    }
    if (key.empty()) {
        spdlog::warn("The key [{}] is empty, you can't delete it from cache group", key);
        return false;
    }
    cache_->Delete(key);
    // 必须是本身这个节点设置的值，才会同步到其他节点
    if (!is_from_peer && peer_picker_) {
        SyncToPeers(key, SyncFlag::DELETE, ByteView{""});
    }
    return true;
}

bool CacheGroup::Invalidate(const std::string& key) {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return false;
    }
    if (key.empty()) {
        spdlog::warn("The key [{}] is empty, you can't invalidate it from cache group", key);
        return false;
    }

    // 主动失效：删除本地缓存并通知其他节点
    cache_->Delete(key);
    spdlog::debug("Invalidated key [{}] from local cache", key);

    // 通知其他节点也失效这个key
    if (peer_picker_) {
        SyncToPeers(key, SyncFlag::INVALIDATE, ByteView{""});
    }

    return true;
}

bool CacheGroup::InvalidateFromPeer(const std::string& key) {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return false;
    }
    if (key.empty()) {
        spdlog::warn("The key [{}] is empty, you can't invalidate it from cache group", key);
        return false;
    }

    // 来自其他节点的失效请求，删除本地缓存
    cache_->Delete(key);
    spdlog::debug("Invalidated key [{}] from local cache (from peer)", key);
    return true;
}

void CacheGroup::SyncToPeers(const std::string& key, SyncFlag op, ByteView value) {
    if (!peer_picker_) {
        return;
    }

    switch (op) {
        case SyncFlag::SET: {
            // 对于 SET 操作，实现最终一致性：
            // 1. 将数据同步到管理该 key 的节点
            auto primary_peer = peer_picker_->PickPeer(key);
            if (primary_peer) {
                bool ok = primary_peer->Set(name_, key, value);
                if (!ok) {
                    spdlog::warn("Failed to sync SET to primary peer for key: {}", key);
                }
            }

            // 2. 向所有其他节点（除了主节点）发送失效通知，保证最终一致性
            auto all_peers = peer_picker_->GetAllPeers();
            for (auto peer : all_peers) {
                if (peer != primary_peer) {  // 排除已经同步过的主节点
                    bool ok = peer->Invalidate(name_, key);
                    if (!ok) {
                        spdlog::warn("Failed to invalidate key [{}] on peer", key);
                    }
                }
            }
            spdlog::debug("SET operation synced: key [{}] set on primary node, invalidated on {} other nodes", key,
                          all_peers.size() - (primary_peer ? 1 : 0));
            break;
        }
        case SyncFlag::DELETE: {
            // 对于 DELETE 操作，需要广播到所有节点
            auto all_peers = peer_picker_->GetAllPeers();
            for (auto peer : all_peers) {
                bool ok = peer->Delete(name_, key);
                if (!ok) {
                    spdlog::warn("Failed to sync DELETE to peer for key: {}", key);
                }
            }
            spdlog::debug("DELETE operation synced: key [{}] deleted on {} nodes", key, all_peers.size());
            break;
        }
        case SyncFlag::INVALIDATE: {
            // 向所有其他节点发送失效通知
            auto all_peers = peer_picker_->GetAllPeers();
            for (auto peer : all_peers) {
                bool ok = peer->Invalidate(name_, key);
                if (!ok) {
                    spdlog::warn("Failed to invalidate key [{}] on peer", key);
                }
            }
            spdlog::debug("INVALIDATE operation synced: key [{}] invalidated on {} nodes", key, all_peers.size());
            break;
        }
        default:
            spdlog::warn("Unknown sync operation: {}", static_cast<int>(op));
            return;
    }
}

void CacheGroup::RegisterPeerPicker(std::unique_ptr<PeerPicker>&& peers) {
    if (peer_picker_) {
        throw std::runtime_error("call RegisterPeers more than once!");
    }
    peer_picker_ = std::move(peers);
}

auto CacheGroup::Load(const std::string& key) -> ByteViewOptional {
    auto ret = loader_.Do(key, [&] { return LoadData(key); });
    if (!ret) {
        spdlog::error("Failed to load data for key: {}", key);
        return std::nullopt;
    }
    cache_->Set(key, ret.value());
    // TODO 记录加载时间
    return ret;
}

auto CacheGroup::LoadData(const std::string& key) -> ByteViewOptional {
    // 先尝试从远程节点获取
    if (peer_picker_ != nullptr) {
        if (auto peer = peer_picker_->PickPeer(key); peer) {
            auto val = LoadFromPeer(peer, key);
            if (val) {
                ++status_.peer_hits;
                return val;
            }
            ++status_.peer_misses;
        } else {
            spdlog::info("Try to load key [{}] from local", key);
        }
    }

    // 通过getter从数据源获取
    auto val = getter_(key);
    if (!val) {
        return std::nullopt;
    }
    ++status_.local_hits;
    return val;
}

auto CacheGroup::LoadFromPeer(Peer* peer, const std::string& key) -> ByteViewOptional {
    auto value = peer->Get(name_, key);
    if (!value) {
        return std::nullopt;
    }
    return value;
}

}  // namespace kcache