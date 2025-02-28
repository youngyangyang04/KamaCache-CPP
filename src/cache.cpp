#include "cache.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

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
        std::cout << "[Cache] cache hit\n";
        return ret;
    }
    return Load(key);
}

auto CacheGroup::Load(const std::string& key) -> std::optional<ValueRef> { return LoadFromLocal(key); }

auto CacheGroup::LoadFromLocal(const std::string& key) -> std::optional<ValueRef> {
    auto ret = getter_(key);
    if (!ret) {
        return ret;
    }
    auto value = ret.value();
    cache_->Put(key, value);
    return value;
}

}  // namespace kcache