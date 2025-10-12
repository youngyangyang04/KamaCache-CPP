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

bool CacheGroup::Set(const std::string& key, ByteView b) {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return false;
    }
    if (key.empty()) {
        spdlog::warn("The key [{}] is empty, you can't set it into cache group", key);
        return false;
    }
    cache_->Set(key, b);
    spdlog::debug("key:{} is set value:{}", key, b.ToString());
    return true;
}

bool CacheGroup::Delete(const std::string& key) {
    if (is_close_) {
        spdlog::error("Cache group [{}] is closed!!!", name_);
        return false;
    }
    if (key.empty()) {
        spdlog::warn("The key [{}] is empty, you can't delete it from cache group", key);
        return false;
    }
    cache_->Delete(key);
    spdlog::debug("key:{} is deleted", key);
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
    spdlog::info("Try to load key [{}] from local", key);
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