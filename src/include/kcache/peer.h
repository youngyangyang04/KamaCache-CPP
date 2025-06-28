#ifndef HTTP_H_
#define HTTP_H_

#include <fmt/core.h>
#include <grpcpp/channel.h>
#include <grpcpp/grpcpp.h>

#include <etcd/Client.hpp>
#include <etcd/Response.hpp>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "kcache.grpc.pb.h"
#include "kcache/cache.h"
#include "kcache/consistent_hash.h"

namespace kcache {

/**
 * @brief 代表一个服务节点
 *
 */
class Peer {
public:
    explicit Peer(const std::string& addr);

    auto Get(const std::string& group_name, const std::string& key) -> ByteViewOptional;
    bool Set(const std::string& group_name, const std::string& key, ByteView data);
    bool Delete(const std::string& group_name, const std::string& key);

private:
    std::string addr_;  // addr = ip:port，且 addr 将作为这个节点在一致性哈希环中的 name
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<pb::KCache::Stub> grpc_client_;
};

class PeerPicker {
public:
    PeerPicker(const std::string& addr, const std::string& service_name, HashConfig cfg = kDefaultConfig);
    ~PeerPicker();

    // 选择一个 Peer 节点
    auto PickPeer(const std::string& key) -> Peer*;

private:
    bool StartServiceDiscovery();
    void WatchServiceChanges();
    void HandleWatchEvents(const etcd::Response& resp);
    bool FetchAllServices();
    void Set(const std::string& addr);
    void Remove(const std::string& addr);
    auto ParseAddrFromKey(const std::string& key) -> std::string;

private:
    std::string self_addr_;
    std::string service_name_;
    std::shared_mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;  // 节点名称和节点本身的映射
    std::atomic_bool is_stop_watch_;
    std::shared_ptr<etcd::Client> etcd_client_;
    std::unique_ptr<etcd::Watcher> etcd_watcher_;
    std::thread etcd_watch_thread_;
    ConsistentHashMap cons_hash_;  // 一致性哈希，存储所有（虚拟）节点的哈希值
};

}  // namespace kcache

#endif /* HTTP_H_ */
