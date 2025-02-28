#ifndef CACHE_H_
#define CACHE_H_

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lru.h"
namespace kcache {

struct ByteValue : public Value {
    std::vector<char> data_{};

    ByteValue(const std::string& str) {
        data_.resize(str.size());
        std::copy(str.begin(), str.end(), data_.begin());
    }

    auto Len() -> int64_t override { return data_.size(); }

    auto ToString() -> std::string override { return std::string(data_.begin(), data_.end()); }
};

using Getter = std::function<std::optional<ValueRef>(const std::string& key)>;

class CacheGroup {
public:
    CacheGroup() = default;

    CacheGroup(std::string name, int64_t bytes, Getter getter)
        : cache_(std::make_unique<LRUCache>(bytes)), name_(name), getter_(getter) {}

    CacheGroup(const CacheGroup&) = delete;

    auto operator=(const CacheGroup& other) -> CacheGroup& = delete;

    CacheGroup(CacheGroup&& other) {
        cache_ = std::move(other.cache_);
        name_ = std::move(other.name_);
        getter_ = std::move(other.getter_);
    }

    auto operator=(CacheGroup&& other) -> CacheGroup& {
        cache_ = std::move(other.cache_);
        name_ = std::move(other.name_);
        getter_ = std::move(other.getter_);
        return *this;
    }

    auto Get(const std::string& key) -> std::optional<ValueRef>;

private:
    auto Load(const std::string& key) -> std::optional<ValueRef>;
    auto LoadFromLocal(const std::string& key) -> std::optional<ValueRef>;

private:
    std::unique_ptr<LRUCache> cache_;
    std::string name_;
    Getter getter_;
};

auto NewCacheGroup(const std::string& name, int64_t bytes, Getter getter) -> CacheGroup&;
auto GetCacheGroup(const std::string& name) -> CacheGroup*;

}  // namespace kcache

#endif /* CACHE_H_ */
