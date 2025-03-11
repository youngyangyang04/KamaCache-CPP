#ifndef CONSISTENT_HASH_H_
#define CONSISTENT_HASH_H_

#include <zconf.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace kcache {

constexpr uint32_t default_relicas = 10;

class ConsistentHash {
public:
    using HashFunc = std::function<uint32_t(const std::string&)>;

    ConsistentHash() = default;

    explicit ConsistentHash(uint32_t replicas, HashFunc hash = std::hash<std::string>{})
        : replicas_(replicas), hash_(hash) {}

    /**
     * @brief 添加真实节点，为其生成多个对应的虚拟节点放入哈希环中
     *
     * @param nodes
     */
    void AddNodes(const std::vector<std::string>& nodes) {
        for (const auto& node : nodes) {
            for (uint32_t i = 0; i < replicas_; ++i) {
                std::string virtual_node = std::to_string(i) + node;
                uint32_t hash_value = hash_(virtual_node);
                node_hash_val_.emplace_back(hash_value);
                map_[hash_value] = node;
            }
        }
        // 为了方便一致性哈希中获取某个key对应的节点，需要对所有的虚拟节点按照哈希值大小排序
        std::ranges::sort(node_hash_val_);
    }

    /**
     * @brief 获取给定 key 对应的 node 节点的名称
     *
     * @param key
     * @return std::string
     */
    auto GetNodeName(const std::string& key) -> std::string {
        if (node_hash_val_.empty()) {
            return "";
        }
        uint32_t hash_value = hash_(key);
        auto it = std::lower_bound(node_hash_val_.begin(), node_hash_val_.end(), hash_value);
        // 处理回环情况
        if (it == node_hash_val_.end()) {
            it = node_hash_val_.begin();
        }
        return map_.at(*it);
    }

    void PrintHashRing() {
        int i = 1;
        for (const auto& node_hash_val : node_hash_val_) {
            std::cout << std::format("{}. {}\n", i++, map_[node_hash_val]);
        }
    }

private:
    /**
     * @brief zlib 中 crc32 函数的包装，不过在本项目的一致性哈希中，直接使用了标准库的 std::hash<std::string>
     *
     * @param data
     * @return uint32_t
     */
    static auto ZlibCRC32Wrapper(const std::string& data) -> uint32_t {
        return crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
    }

private:
    uint32_t replicas_;                              // 虚拟节点的倍数，即每个节点应该对应多少个虚拟节点
    HashFunc hash_;                                  // 哈希函数，默认使用 zlib 提供的 crc32
    std::vector<uint32_t> node_hash_val_;            // 所有虚拟节点的哈希值
    std::unordered_map<uint32_t, std::string> map_;  // 虚拟节点的哈希值与节点名称的映射
};

}  // namespace kcache

#endif /* CONSISTENT_HASH_H_ */
