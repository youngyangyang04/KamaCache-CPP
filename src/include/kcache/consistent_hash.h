#ifndef CONSISTENT_HASH_H_
#define CONSISTENT_HASH_H_

#include <fmt/core.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kcache {

// Config 一致性哈希配置
struct Config {
    // 每个真实节点对应的虚拟节点数
    int replicas;
    // 最小虚拟节点数
    int min_replicas;
    // 最大虚拟节点数
    int max_replicas;
    // 哈希函数
    std::function<uint32_t(const std::string&)> hash_func;
    // 负载均衡阈值，超过此值触发虚拟节点调整
    double load_balance_threshold;
};

// DefaultConfig 默认配置
const Config DefaultConfig = {
    50,   10, 200, std::hash<std::string>{},
    0.25,  // 25% 的负载不均衡度触发调整
};

// Map 一致性哈希实现
class ConsistentHashMap {
public:
    // New 创建一致性哈希实例
    explicit ConsistentHashMap(const Config& cfg = DefaultConfig);

    // 析构函数，确保负载均衡器线程正确停止
    ~ConsistentHashMap();

    // Add 添加节点
    // 返回 true 表示成功，false 表示失败
    bool Add(const std::vector<std::string>& nodes);

    // Remove 移除节点
    // 返回 true 表示成功，false 表示失败
    bool Remove(const std::string& node);

    // Get 获取节点
    auto Get(const std::string& key) -> std::string;

    // GetStats 获取负载统计信息
    auto GetStats() -> std::unordered_map<std::string, double>;

private:
    // addNode 添加节点的虚拟节点
    void AddNode(const std::string& node, int replicas);

    // checkAndRebalance 检查并重新平衡虚拟节点
    void CheckAndRebalance();

    // rebalanceNodes 重新平衡节点
    void RebalanceNodes();

    // startBalancer 启动负载均衡器线程
    void StartBalancer();

private:
    mutable std::shared_mutex mtx_;  // 读写互斥量，类似于 Go 的 sync.RWMutex
    // 配置信息
    Config config_;

    // 哈希环
    std::vector<int> keys_;
    // 哈希环到节点的映射
    std::unordered_map<int, std::string> hash_map_;
    // 节点到虚拟节点数量的映射
    std::unordered_map<std::string, int> node_replicas_;
    // 节点负载统计
    // 使用 std::atomic<long long> 保证对 nodeCounts 中每个节点计数的原子操作
    std::unordered_map<std::string, std::atomic<long long>> node_counts_;
    // 总请求数
    std::atomic<long long> total_requests_;

    std::thread balancer_thread_;         // 负载均衡器线程
    std::atomic<bool> is_balancer_stop_;  // 控制负载均衡器线程停止的标志
};

}  // namespace kcache

#endif /* CONSISTENT_HASH_H_ */
