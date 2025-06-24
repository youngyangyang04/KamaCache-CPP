#include "kcache/consistent_hash.h"

#include <fmt/base.h>
#include <fmt/format.h>

namespace kcache {

ConsistentHashMap::ConsistentHashMap(const Config& cfg) : config_(cfg), total_requests_(0), is_balancer_stop_(false) {
    StartBalancer();  // 启动负载均衡器
}

ConsistentHashMap::~ConsistentHashMap() {
    is_balancer_stop_ = true;
    if (balancer_thread_.joinable()) {
        balancer_thread_.join();  // 等待负载均衡器线程完成
    }
}

bool ConsistentHashMap::Add(const std::vector<std::string>& nodes) {
    if (nodes.empty()) {
        return false;
    }

    std::unique_lock lock{mtx_};  // 获取写锁

    for (const auto& node : nodes) {
        if (node.empty()) {
            continue;
        }
        // 为节点添加虚拟节点
        AddNode(node, config_.replicas);
    }

    // 重新排序哈希环
    std::sort(keys_.begin(), keys_.end());
    return true;
}

bool ConsistentHashMap::Remove(const std::string& node) {
    if (node.empty()) {
        return false;
    }

    std::unique_lock lock{mtx_};  // 获取写锁

    auto it_replicas = node_replicas_.find(node);
    if (it_replicas == node_replicas_.end()) {
        return false;  // 节点未找到
    }

    int replicas = it_replicas->second;

    // 移除节点的所有虚拟节点
    for (int i = 0; i < replicas; ++i) {
        std::string hash_key = fmt::format("{}-{}", node, std::to_string(i));
        int hash = static_cast<int>(config_.hash_func(hash_key));
        hash_map_.erase(hash);  // 从哈希映射中移除
        // 从哈希环中移除哈希值
        auto it = std::remove(keys_.begin(), keys_.end(), hash);
        keys_.erase(it, keys_.end());
    }

    node_replicas_.erase(node);
    node_counts_.erase(node);  // 从负载统计中移除
    return true;
}

auto ConsistentHashMap::Get(const std::string& key) -> std::string {
    if (key.empty()) {
        return "";
    }

    std::shared_lock lock{mtx_};  // 获取读锁

    if (keys_.empty()) {
        return "";
    }

    int hash = static_cast<int>(config_.hash_func(key));
    // 二分查找：找到第一个大于等于 hash 的位置
    auto it = std::lower_bound(keys_.begin(), keys_.end(), hash);

    // 处理边界情况：如果到了末尾，则回到开头
    if (it == keys_.end()) {
        it = keys_.begin();
    }

    std::string node = hash_map_[*it];
    // 增加节点计数和总请求数，这里使用原子操作
    ++node_counts_[node];
    ++total_requests_;

    return node;
}

auto ConsistentHashMap::GetStats() -> std::unordered_map<std::string, double> {
    std::shared_lock lock{mtx_};  // 获取读锁

    std::unordered_map<std::string, double> stats;
    long long curr_total = total_requests_.load();
    if (curr_total == 0) {
        return stats;
    }

    for (auto const& [node, count] : node_counts_) {
        stats[node] = static_cast<double>(count.load()) / static_cast<double>(curr_total);
    }
    return stats;
}

void ConsistentHashMap::StartBalancer() {
    is_balancer_stop_ = false;
    balancer_thread_ = std::thread{[this] {
        while (!is_balancer_stop_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));  // 每秒检查一次
            if (!is_balancer_stop_) {                              // 再次检查，防止在 sleep 期间被要求停止
                CheckAndRebalance();
            }
        }
    }};
}

void ConsistentHashMap::AddNode(const std::string& node, int replicas) {
    for (int i = 0; i < replicas; ++i) {
        std::string hash_key = fmt::format("{}-{}", node, std::to_string(i));
        int hash = static_cast<int>(config_.hash_func(hash_key));
        if (hash_map_.find(hash) != hash_map_.end()) {
            continue;
        }
        keys_.push_back(hash);
        hash_map_[hash] = node;
    }
    node_replicas_[node] = replicas;
    // 如果节点是新添加的，初始化其计数器
    if (node_counts_.find(node) == node_counts_.end()) {
        node_counts_[node] = 0;
    }
}

void ConsistentHashMap::CheckAndRebalance() {
    if (total_requests_.load() < 1000) {
        return;  // 样本太少，不进行调整
    }

    std::shared_lock lock{mtx_};

    if (node_replicas_.empty()) {
        return;
    }

    // 计算系统平均负载：总请求数 / 物理节点数量
    long long current_total_requests = total_requests_.load();
    double avg_load = static_cast<double>(current_total_requests) / node_replicas_.size();
    double max_diff = 0.0;

    // 遍历所有节点计算负载偏差，计算每个节点的负载与平均负载的差异百分比
    for (auto const& [node, count] : node_counts_) {
        double diff = std::abs(static_cast<double>(count.load()) - avg_load);
        // 避免除以零
        if (avg_load > 0) {
            if (diff / avg_load > max_diff) {
                max_diff = diff / avg_load;
            }
        } else if (diff > 0) {  // If avgLoad is 0 but there are counts, it's imbalanced
            max_diff = 1.0;     // Max imbalance
        }
    }
    lock.unlock();  // 释放读锁，因为 rebalanceNodes 需要写锁

    // 如果负载不均衡度超过阈值，调整虚拟节点
    if (max_diff > config_.load_balance_threshold) {
        RebalanceNodes();
    }
}

void ConsistentHashMap::RebalanceNodes() {
    std::unique_lock lock{mtx_};  // 获取写锁

    if (node_replicas_.empty()) {
        return;
    }

    long long current_total_requests = total_requests_.load();
    double avg_load = static_cast<double>(current_total_requests) / node_replicas_.size();

    // 调整每个节点的虚拟节点数量
    // 注意：这里需要创建一个副本，因为在循环中可能会修改 nodeReplicas 和 nodeCounts
    std::unordered_map<std::string, int> curr_replicas = node_replicas_;
    std::unordered_map<std::string, long long> curr_counts;
    for (auto const& [node, count] : node_counts_) {
        curr_counts[node] = count.load();
    }

    for (auto const& [node, count] : curr_counts) {
        int old_replicas = curr_replicas[node];
        double load_ratio = 0.0;
        if (avg_load > 0) {
            load_ratio = static_cast<double>(count) / avg_load;
        } else if (count > 0) {
            load_ratio = 2.0;
        } else {
            load_ratio = 1.0;
        }

        int new_replicas;
        if (load_ratio > 1.0) {
            // 负载过高，减少虚拟节点
            new_replicas = static_cast<int>(std::round(static_cast<double>(old_replicas) / load_ratio));
        } else {
            // 负载过低，增加虚拟节点
            new_replicas = static_cast<int>(std::round(static_cast<double>(old_replicas) * (2.0 - load_ratio)));
        }

        // 确保在限制范围内
        if (new_replicas < config_.min_replicas) {
            new_replicas = config_.min_replicas;
        }
        if (new_replicas > config_.max_replicas) {
            new_replicas = config_.max_replicas;
        }

        if (new_replicas != old_replicas) {
            // 重新添加节点的虚拟节点：先移除旧的，再添加新的
            // 移除节点的所有虚拟节点
            int replicas_to_remove = node_replicas_[node];
            for (int i = 0; i < replicas_to_remove; ++i) {
                std::string hashKey = node + "-" + std::to_string(i);
                int hash = static_cast<int>(config_.hash_func(hashKey));
                hash_map_.erase(hash);
                auto it = std::remove(keys_.begin(), keys_.end(), hash);
                keys_.erase(it, keys_.end());
            }
            node_replicas_.erase(node);

            // 添加新的虚拟节点
            AddNode(node, new_replicas);
        }
    }

    // 重置计数器
    for (auto& pair : node_counts_) {
        pair.second.store(0);
    }
    total_requests_.store(0);

    // 重新排序哈希环
    std::sort(keys_.begin(), keys_.end());
}

}  // namespace kcache