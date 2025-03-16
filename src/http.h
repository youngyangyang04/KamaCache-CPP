#ifndef HTTP_H_
#define HTTP_H_

#include <httplib.h>

#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "consistent_hash.h"
#include "kcache.pb.h"

namespace kcache {

constexpr std::string default_base_path = "/kcache/";

/**
 * @brief 代表一个服务节点
 *
 */
class Peer {
public:
    Peer() = default;

    explicit Peer(const std::string& base_url, const std::string& base_path)
        : base_url_(base_url), base_path_(base_path) {}

    auto Get(pb::Request* req, pb::Response* res) -> bool;

private:
    std::string base_url_;
    std::string base_path_;
};

class HTTPPool {
public:
    HTTPPool(const std::string& host = "0.0.0.0", int port = 8080, const std::string& base_path = default_base_path)
        : host_(host), port_(port), self_(std::format("http://{}:{}", host, port)), base_path_(base_path) {
        server_.Get(".*", [&](const httplib::Request& req, httplib::Response& res) { HandleRequest(req, res); });
    }

    void Start() { server_.listen(host_, port_); }

    auto GetHost() -> std::string { return host_; }
    auto GetPort() -> int { return port_; }

    /**
     * @brief （重新）设置分布式 peer 节点
     *
     * @param peer_addrs 分布式节点的 base_url 的集合
     */
    void SetPeers(const std::vector<std::string>& peer_addrs);

    /**
     * @brief 根据 peer 获取节点
     *
     * @param key
     * @return Peer&
     */
    auto GetPeer(const std::string& key) -> Peer*;

private:
    void HandleRequest(const httplib::Request& request, httplib::Response& response);

private:
    std::string host_;
    int port_;
    std::string self_;
    std::string base_path_;
    std::mutex mtx_;
    std::unordered_map<std::string, Peer> peers_;  // 节点名称和节点本身的映射
    httplib::Server server_;                       // 主服务器
    ConsistentHash peers_hash_;                    // 一致性哈希，存储所有（虚拟）节点的哈希值
};

}  // namespace kcache

#endif /* HTTP_H_ */
