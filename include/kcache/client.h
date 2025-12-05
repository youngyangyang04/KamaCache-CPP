#ifndef KCACHE_CLIENT_H_
#define KCACHE_CLIENT_H_

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

#include <etcd/Client.hpp>
#include <etcd/Watcher.hpp>

#include "kcache/consistent_hash.h"

namespace kcache {

class KCacheClient {
public:
    KCacheClient(const std::string& etcd_endpoints, const std::string& service_name = "kcache");
    ~KCacheClient();

    // 禁止拷贝和赋值
    KCacheClient(const KCacheClient&) = delete;
    KCacheClient& operator=(const KCacheClient&) = delete;

    // 获取缓存
    auto Get(const std::string& group, const std::string& key) -> std::optional<std::string>;

    // 设置缓存
    bool Set(const std::string& group, const std::string& key, const std::string& value);

    // 删除缓存
    bool Delete(const std::string& group, const std::string& key);

private:
    // 服务发现相关
    bool StartServiceDiscovery();
    void HandleWatchEvents(const etcd::Response& resp);
    bool FetchAllServices();
    auto ParseAddrFromKey(const std::string& key) -> std::string;
    auto GetCacheNode(const std::string& key) -> std::string;

private:
    std::string service_name_;
    std::shared_ptr<etcd::Client> etcd_client_;

    std::unordered_set<std::string> cache_nodes_;
    std::mutex nodes_mutex_;

    std::thread discovery_thread_;
    std::unique_ptr<etcd::Watcher> etcd_watcher_;

    ConsistentHashMap consistent_hash_;
};

}  // namespace kcache

#endif  // KCACHE_CLIENT_H_
