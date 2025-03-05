#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>

#include "consistent_hash.h"

TEST(ConsistentHashTest, TestHash) {
    kcache::ConsistentHash hash{3, [](const std::string& key) -> uint32_t { return std::stoi(key); }};

    // 添加下列节点后，哈希环中现在的节点为：2, 4, 6, 12, 14, 16, 22, 24, 26
    hash.AddNodes({"6", "4", "2"});

    // 测试案例中 k 为 key 值，v 为 key 应该对应的节点的 namespace
    // 例如，离“11”的节点的哈希值为“12”，其对应的节点的 name 为“2”
    std::unordered_map<std::string, std::string> test_cases = {
        {"2", "2"},
        {"11", "2"},
        {"23", "4"},
        {"27", "2"},
    };

    for (const auto& [k, v] : test_cases) {
        auto node = hash.GetNodeName(k);
        if (node != v) {
            FAIL() << std::format("Get key: {}, now the node of it is: {}, but should be: {}\n", k, node, v);
        }
    }
    std::cout << "================\n";

    // 现在变为了：2, 4, 6, 8, 12, 14, 16, 18, 22, 24, 26, 28
    hash.AddNodes({"8"});

    // “27”此时应该对应到“8”
    test_cases["27"] = "8";

    for (const auto& [k, v] : test_cases) {
        auto node = hash.GetNodeName(k);
        if (node != v) {
            FAIL() << std::format("Get key: {}, now the node of it is: {}, but should be: {}\n", k, node, v);
        }
    }
}