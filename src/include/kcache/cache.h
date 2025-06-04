#ifndef LRU_H_
#define LRU_H_

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace kcache {

struct ByteView {
    std::vector<char> data_{};

    ByteView(const std::string& str) {
        data_.resize(str.size());
        std::copy(str.begin(), str.end(), data_.begin());
    }

    auto Len() const -> int64_t { return data_.size(); }

    auto ToString() const -> std::string { return std::string(data_.begin(), data_.end()); }
};

using ByteViewOptional = std::optional<ByteView>;

struct Entry {
    std::string key_;
    ByteView value_;

    Entry(std::string k, const ByteView& v) : key_(std::move(k)), value_(v) {}

    auto operator==(const Entry& entry) const -> bool {
        return key_ == entry.key_ && value_.ToString() == entry.value_.ToString();
    }
};

class LRUCache {
    using EvictedFunc = std::function<void(std::string, ByteView)>;
    using ListElementIter = std::list<Entry>::iterator;

public:
    LRUCache(int max_bytes, const EvictedFunc& evicted_func = nullptr)
        : max_bytes_(max_bytes), evicted_func_(evicted_func) {}

    auto Get(const std::string& key) -> ByteViewOptional;
    void Set(const std::string& key, const ByteView&);
    void Delete(const std::string& key);
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
