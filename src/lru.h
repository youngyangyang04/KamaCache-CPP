#ifndef LRU_H_
#define LRU_H_

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace kcache {

struct Value {
    virtual ~Value() = default;
    virtual auto Len() -> int64_t = 0;
    virtual auto ToString() -> std::string = 0;
};

using ValueRef = std::shared_ptr<Value>;

struct Entry {
    std::string key_;
    ValueRef value_;

    auto operator==(const Entry& entry) const -> bool {
        return key_ == entry.key_ && value_->ToString() == entry.value_->ToString();
    }
};

class LRUCache {
    using EvictedFunc = std::function<void(std::string, ValueRef)>;
    using ListElementIter = typename std::list<Entry>::iterator;

public:
    LRUCache(int max_bytes, const EvictedFunc& evicted_func = nullptr)
        : max_bytes_(max_bytes), evicted_func_(evicted_func) {}

    auto Get(const std::string& key) -> std::optional<ValueRef>;
    void Put(const std::string& key, const ValueRef&);
    void RemoveOldest();

private:
    int64_t bytes_ = 0;
    int64_t max_bytes_;
    EvictedFunc evicted_func_;

    std::unordered_map<std::string, ListElementIter> cache_;
    std::list<Entry> list_;
    std::mutex mtx_;
};

}  // namespace kcache

#endif /* LRU_H_ */
