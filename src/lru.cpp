#include "lru.h"

#include <mutex>
#include <optional>

namespace kcache {

auto LRUCache::Get(const std::string& key) -> std::optional<ValueRef> {
    std::lock_guard _{mtx_};
    if (!cache_.contains(key)) {
        return std::nullopt;
    }
    auto ele = cache_[key];
    auto [_, value] = *ele;
    list_.erase(ele);
    list_.emplace_front(key, value);
    cache_[key] = list_.begin();
    return value;
}

void LRUCache::Put(const std::string& key, const ValueRef& value) {
    std::lock_guard _{mtx_};
    if (cache_.contains(key)) {
        // remove old
        auto ele = cache_[key];
        bytes_ += value->Len() - ele->value_->Len();
        list_.erase(ele);
    } else {
        bytes_ += key.size() + value->Len();
    }
    // insert new
    list_.emplace_front(key, value);
    cache_[key] = list_.begin();

    // 当 LRUCache 中还有缓存时，如果此时 LRUCache 中的容量超过规定大小，就不断将最久未使用的缓存淘汰
    while (max_bytes_ != 0 && bytes_ > max_bytes_ && !list_.empty()) {
        RemoveOldest();
    }
}

void LRUCache::RemoveOldest() {
    if (list_.empty()) {
        return;
    }
    auto [key, value] = list_.back();
    cache_.erase(key);
    list_.pop_back();
    bytes_ -= key.size() + value->Len();
    if (evicted_func_) {
        evicted_func_(key, value);
    }
}

}  // namespace kcache