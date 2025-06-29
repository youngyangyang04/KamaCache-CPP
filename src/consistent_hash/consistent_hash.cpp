#include "kcache/consistent_hash.h"

#include <mutex>

#include <fmt/base.h>
#include <fmt/format.h>

namespace kcache {

// CRC32 IEEE 查找表，兼容 Go 的 crc32.ChecksumIEEE
static constexpr uint32_t kCrc32IEEETable[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832,
    0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a,
    0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
    0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab,
    0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4,
    0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074,
    0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525,
    0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
    0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76,
    0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6,
    0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7,
    0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7,
    0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
    0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330,
    0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

uint32_t Crc32IEEE(const std::string& data) {
    uint32_t crc = 0xFFFFFFFF;
    for (char c : data) {
        crc = kCrc32IEEETable[(crc ^ static_cast<uint8_t>(c)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

ConsistentHashMap::ConsistentHashMap(HashConfig cfg) : config_(cfg), total_requests_(0), is_balancer_stop_(false) {
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
            if (!is_balancer_stop_) {  // 再次检查，防止在 sleep 期间被要求停止
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