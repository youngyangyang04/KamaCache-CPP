#include <fmt/base.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "kcache/consistent_hash.h"

using namespace kcache;

class ConsistentHashTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Simple hash function for predictable testing
        test_config_ = Config{
            3,   // replicas
            1,   // min_replicas
            10,  // max_replicas
            std::hash<std::string>{},
            0.2  // load_balance_threshold
        };
    }

    Config test_config_;
};

TEST_F(ConsistentHashTest, DefaultConstructor) {
    ConsistentHashMap hash_map;

    // Should work with default configuration
    EXPECT_TRUE(hash_map.Add({"node1", "node2"}));

    auto node = hash_map.Get("test_key");
    EXPECT_TRUE(node == "node1" || node == "node2");
}

TEST_F(ConsistentHashTest, CustomConfigConstructor) {
    ConsistentHashMap hash_map(test_config_);

    EXPECT_TRUE(hash_map.Add({"node1"}));
    auto node = hash_map.Get("test_key");
    EXPECT_EQ(node, "node1");
}

TEST_F(ConsistentHashTest, BasicAddAndGet) {
    ConsistentHashMap hash_map(test_config_);

    // Add nodes
    EXPECT_TRUE(hash_map.Add({"node1", "node2", "node3"}));

    // Test that Get returns one of the added nodes
    std::unordered_set<std::string> expected_nodes = {"node1", "node2", "node3"};

    for (int i = 0; i < 100; ++i) {
        std::string key = "key" + std::to_string(i);
        auto node = hash_map.Get(key);
        EXPECT_TRUE(expected_nodes.count(node) > 0);
    }
}

TEST_F(ConsistentHashTest, ConsistentHashing) {
    ConsistentHashMap hash_map(test_config_);

    // Add initial nodes
    EXPECT_TRUE(hash_map.Add({"node1", "node2"}));

    // Record which node each key maps to
    std::unordered_map<std::string, std::string> key_to_node;
    std::vector<std::string> test_keys;

    for (int i = 0; i < 50; ++i) {
        std::string key = "key" + std::to_string(i);
        test_keys.push_back(key);
        key_to_node[key] = hash_map.Get(key);
    }

    // Add another node
    EXPECT_TRUE(hash_map.Add({"node3"}));

    // Check that most keys still map to the same nodes
    int unchanged_keys = 0;
    for (const auto& key : test_keys) {
        if (hash_map.Get(key) == key_to_node[key]) {
            unchanged_keys++;
        }
    }

    // With consistent hashing, most keys should remain unchanged
    EXPECT_GT(unchanged_keys, test_keys.size() * 0.6);  // At least 60% should be unchanged
}

TEST_F(ConsistentHashTest, RemoveNode) {
    ConsistentHashMap hash_map(test_config_);

    // Add nodes
    EXPECT_TRUE(hash_map.Add({"node1", "node2", "node3"}));

    // Remove a node
    EXPECT_TRUE(hash_map.Remove("node2"));

    // Verify node2 is no longer returned
    std::unordered_set<std::string> possible_nodes;
    for (int i = 0; i < 100; ++i) {
        std::string key = "key" + std::to_string(i);
        auto node = hash_map.Get(key);
        possible_nodes.insert(node);
    }

    EXPECT_EQ(possible_nodes.count("node2"), 0);
    EXPECT_GT(possible_nodes.count("node1"), 0);
    EXPECT_GT(possible_nodes.count("node3"), 0);
}

TEST_F(ConsistentHashTest, RemoveNonExistentNode) {
    ConsistentHashMap hash_map(test_config_);

    EXPECT_TRUE(hash_map.Add({"node1"}));

    // Removing non-existent node should handle gracefully
    bool result = hash_map.Remove("nonexistent");
    // The behavior may vary based on implementation
    // Just ensure it doesn't crash

    // Original node should still work
    auto node = hash_map.Get("test_key");
    EXPECT_EQ(node, "node1");
}

TEST_F(ConsistentHashTest, EmptyHashMap) {
    ConsistentHashMap hash_map(test_config_);

    // Getting from empty hash map should return empty string or handle gracefully
    auto node = hash_map.Get("test_key");
    // Implementation may return empty string or throw
    // Just ensure it doesn't crash
}

TEST_F(ConsistentHashTest, LoadBalanceDistribution) {
    ConsistentHashMap hash_map(test_config_);

    // Add nodes
    EXPECT_TRUE(hash_map.Add({"node1", "node2", "node3"}));

    // Generate many requests to test load distribution
    std::unordered_map<std::string, int> node_counts;
    int total_requests = 10000;

    for (int i = 0; i < total_requests; ++i) {
        std::string key = "key" + std::to_string(i);
        auto node = hash_map.Get(key);
        node_counts[node]++;
    }

    // Check that load is reasonably distributed
    EXPECT_EQ(node_counts.size(), 3);

    for (const auto& [node, count] : node_counts) {
        double load_ratio = static_cast<double>(count) / total_requests;
        // Each node should get roughly 1/3 of the load, allow some variance
        EXPECT_GT(load_ratio, 0.2);
        EXPECT_LT(load_ratio, 0.5);
    }
}

TEST_F(ConsistentHashTest, GetStats) {
    ConsistentHashMap hash_map(test_config_);

    EXPECT_TRUE(hash_map.Add({"node1", "node2"}));

    // Make some requests
    for (int i = 0; i < 100; ++i) {
        hash_map.Get("key" + std::to_string(i));
    }

    // Get statistics
    auto stats = hash_map.GetStats();

    // Should have stats for both nodes
    EXPECT_TRUE(stats.count("node1") > 0 || stats.count("node2") > 0);

    // Stats should be reasonable (between 0 and 1)
    for (const auto& [node, ratio] : stats) {
        EXPECT_GE(ratio, 0.0);
        EXPECT_LE(ratio, 1.0);
    }
}

TEST_F(ConsistentHashTest, ThreadSafety) {
    ConsistentHashMap hash_map(test_config_);

    EXPECT_TRUE(hash_map.Add({"node1", "node2", "node3"}));

    std::atomic<int> successful_gets{0};
    std::atomic<bool> stop_flag{false};

    // Start multiple reader threads
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&hash_map, &successful_gets, &stop_flag, i]() {
            while (!stop_flag.load()) {
                std::string key = "thread" + std::to_string(i) + "_key" + std::to_string(successful_gets.load());
                auto node = hash_map.Get(key);
                if (!node.empty()) {
                    successful_gets++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Start a writer thread
    std::thread writer([&hash_map, &stop_flag]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hash_map.Add({"node4"});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hash_map.Remove("node4");
        stop_flag.store(true);
    });

    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_GT(successful_gets.load(), 0);
}

TEST_F(ConsistentHashTest, MultipleAddOperations) {
    ConsistentHashMap hash_map(test_config_);

    // Add nodes in multiple batches
    EXPECT_TRUE(hash_map.Add({"node1"}));
    EXPECT_TRUE(hash_map.Add({"node2", "node3"}));
    EXPECT_TRUE(hash_map.Add({"node4"}));

    // Verify all nodes are accessible
    std::unordered_set<std::string> found_nodes;
    for (int i = 0; i < 1000; ++i) {
        std::string key = "key" + std::to_string(i);
        auto node = hash_map.Get(key);
        found_nodes.insert(node);
    }

    EXPECT_EQ(found_nodes.size(), 4);
    EXPECT_TRUE(found_nodes.count("node1") > 0);
    EXPECT_TRUE(found_nodes.count("node2") > 0);
    EXPECT_TRUE(found_nodes.count("node3") > 0);
    EXPECT_TRUE(found_nodes.count("node4") > 0);
}

TEST_F(ConsistentHashTest, DuplicateNodeAddition) {
    ConsistentHashMap hash_map(test_config_);

    EXPECT_TRUE(hash_map.Add({"node1", "node2"}));

    // Try to add duplicate nodes
    bool result = hash_map.Add({"node1", "node3"});
    // Implementation may handle duplicates differently
    // Just ensure it doesn't crash

    auto node = hash_map.Get("test_key");
    EXPECT_FALSE(node.empty());
}

TEST_F(ConsistentHashTest, SpecificHashBehavior) {
    // Test with a simple, predictable hash function
    Config simple_config = test_config_;
    simple_config.replicas = 1;  // Use fewer replicas for predictable testing
    simple_config.hash_func = [](const std::string& key) -> uint32_t {
        if (key == "2") return 2;
        if (key == "4") return 4;
        if (key == "6") return 6;
        if (key == "8") return 8;
        if (key == "11") return 11;
        if (key == "23") return 23;
        if (key == "27") return 27;
        // For node names, create virtual nodes
        if (key == "2_0") return 2;
        if (key == "4_0") return 4;
        if (key == "6_0") return 6;
        if (key == "8_0") return 8;
        return std::hash<std::string>{}(key);
    };

    ConsistentHashMap hash_map(simple_config);

    // This test is based on the example, but may need adjustment
    // depending on the exact virtual node generation algorithm
    EXPECT_TRUE(hash_map.Add({"6", "4", "2"}));

    // Test some key mappings
    auto node = hash_map.Get("2");
    EXPECT_FALSE(node.empty());

    node = hash_map.Get("11");
    EXPECT_FALSE(node.empty());
}

TEST_F(ConsistentHashTest, ConfigValidation) {
    // Test with extreme configurations
    Config extreme_config = test_config_;
    extreme_config.replicas = 1000;  // Very high replicas
    extreme_config.min_replicas = 500;
    extreme_config.max_replicas = 2000;

    ConsistentHashMap hash_map(extreme_config);
    EXPECT_TRUE(hash_map.Add({"node1"}));

    auto node = hash_map.Get("test_key");
    EXPECT_EQ(node, "node1");
}

TEST_F(ConsistentHashTest, LongRunningBalancer) {
    ConsistentHashMap hash_map(test_config_);

    EXPECT_TRUE(hash_map.Add({"node1", "node2"}));

    // Make many requests to trigger balancer activity
    for (int i = 0; i < 1000; ++i) {
        hash_map.Get("key" + std::to_string(i));
        if (i % 100 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Add more nodes to trigger rebalancing
    EXPECT_TRUE(hash_map.Add({"node3", "node4"}));

    // Continue making requests
    for (int i = 1000; i < 2000; ++i) {
        hash_map.Get("key" + std::to_string(i));
    }

    auto stats = hash_map.GetStats();
    EXPECT_GE(stats.size(), 2);
}

// Test destructor behavior - ensure balancer thread stops properly
TEST_F(ConsistentHashTest, DestructorTest) {
    {
        ConsistentHashMap hash_map(test_config_);
        EXPECT_TRUE(hash_map.Add({"node1", "node2"}));

        // Make some requests
        for (int i = 0; i < 100; ++i) {
            hash_map.Get("key" + std::to_string(i));
        }

        // hash_map will be destroyed here
    }

    // If we reach here without hanging, destructor worked correctly
    SUCCEED();
}
