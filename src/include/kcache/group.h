#ifndef CACHE_H_
#define CACHE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "kcache/cache.h"
#include "kcache/peer.h"
#include "kcache/singleflight.h"

namespace kcache {

using DataGetter = std::function<ByteViewOptional(const std::string& key)>;

struct GroupStatus {
    std::atomic_int64_t loads;          // 加载次数
    std::atomic_int64_t local_hits;     // 本地缓存命中次数
    std::atomic_int64_t local_misses;   // 本地缓存未命中次数
    std::atomic_int64_t peer_hits;      // 从对等节点获取成功次数
    std::atomic_int64_t peer_misses;    // 从对等节点获取失败次数
    std::atomic_int64_t loader_hits;    // 从加载器获取成功次数
    std::atomic_int64_t loader_errors;  // 从加载器获取失败次数
    std::atomic_int64_t load_duration;  // 加载总耗时（纳秒）};
};

enum class SyncFlag {
    SET,
    DELETE,
};

class CacheGroup {
public:
    CacheGroup() = default;

    CacheGroup(std::string name, int64_t bytes, DataGetter getter)
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

    auto Get(const std::string& key) -> ByteViewOptional;

    bool Set(const std::string& key, ByteView b, bool is_from_peer = false);

    bool Delete(const std::string& key, bool is_from_peer = false);

    void SyncToPeers(const std::string& key, SyncFlag op, ByteView value);

    void RegisterPeerPicker(std::unique_ptr<PeerPicker>&& peer_picker);

private:
    auto Load(const std::string& key) -> ByteViewOptional;
    auto LoadData(const std::string& key) -> ByteViewOptional;
    auto LoadFromPeer(Peer* peer, const std::string& key) -> ByteViewOptional;

private:
    std::unique_ptr<LRUCache> cache_;
    std::unique_ptr<PeerPicker> peer_picker_;
    std::string name_;
    std::atomic<bool> is_close_{false};
    DataGetter getter_;
    SingleFlight loader_;
    GroupStatus status_;
};

auto MakeCacheGroup(const std::string& name, int64_t bytes, DataGetter getter) -> CacheGroup&;
auto GetCacheGroup(const std::string& name) -> CacheGroup*;

}  // namespace kcache

#endif /* CACHE_H_ */
