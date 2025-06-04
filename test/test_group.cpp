#include <fmt/base.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "kcache/cache.h"
#include "kcache/group.h"
#include "kcache/peer.h"

using namespace kcache;

class CacheGroupTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = {{"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"}};
        call_count_.clear();

        getter_ = [this](const std::string& key) -> ByteViewOptional {
            call_count_[key]++;
            auto it = db_.find(key);
            if (it != db_.end()) {
                return ByteView{it->second};
            }
            return std::nullopt;
        };
    }

    std::unordered_map<std::string, std::string> db_;
    std::unordered_map<std::string, int> call_count_;
    DataGetter getter_;
};

TEST_F(CacheGroupTest, ConstructorAndBasicOperations) {
    CacheGroup group("test", 1024, getter_);

    // Test Get operation
    auto result = group.Get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);

    // Test cache hit (should not call getter again)
    result = group.Get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);  // Should not increment
}

TEST_F(CacheGroupTest, GetNonExistentKey) {
    CacheGroup group("test", 1024, getter_);

    auto result = group.Get("nonexistent");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(call_count_["nonexistent"], 1);
}

TEST_F(CacheGroupTest, SetOperation) {
    CacheGroup group("test", 1024, getter_);

    // Set a value
    ByteView value("manual_value");
    bool success = group.Set("manual_key", value);
    EXPECT_TRUE(success);

    // Get the set value (should not call getter)
    auto result = group.Get("manual_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().ToString(), "manual_value");
    EXPECT_EQ(call_count_["manual_key"], 0);  // Should not call getter
}

TEST_F(CacheGroupTest, DeleteOperation) {
    CacheGroup group("test", 1024, getter_);

    // First get a value to cache it
    auto result = group.Get("key1");
    ASSERT_TRUE(result.has_value());

    // Delete the key
    bool success = group.Delete("key1");
    EXPECT_TRUE(success);

    // Get again should call getter
    call_count_["key1"] = 0;  // Reset counter
    result = group.Get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count_["key1"], 1);  // Should call getter again
}

TEST_F(CacheGroupTest, MoveConstructor) {
    CacheGroup group1("test", 1024, getter_);

    // Cache a value in group1
    auto result = group1.Get("key1");
    ASSERT_TRUE(result.has_value());

    // Move construct group2
    CacheGroup group2(std::move(group1));

    // group2 should have the cached value
    result = group2.Get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);  // Should not call getter again
}

TEST_F(CacheGroupTest, MoveAssignment) {
    CacheGroup group1("test1", 1024, getter_);
    CacheGroup group2("test2", 512, getter_);

    // Cache a value in group1
    auto result = group1.Get("key1");
    ASSERT_TRUE(result.has_value());

    // Move assign group1 to group2
    group2 = std::move(group1);

    // group2 should have the cached value from group1
    result = group2.Get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);  // Should not call getter again
}

TEST_F(CacheGroupTest, DefaultConstructor) {
    CacheGroup group;

    // Default constructed group should handle operations gracefully
    auto result = group.Get("key1");
    EXPECT_FALSE(result.has_value());
}

// // Mock Peer class for testing
// class MockPeer : public Peer {
// public:
//     mutable std::string last_group;
//     mutable std::string last_key;
//     mutable bool should_succeed = true;
//     mutable ByteView received_value{""};

//     MockPeer() : Peer("localhost", "mock_service") {}

//     auto Get(const std::string& group, const std::string& key) const -> ByteViewOptional {
//         last_group = group;
//         last_key = key;
//         if (should_succeed) {
//             return ByteView{"peer_value"};
//         }
//         return std::nullopt;
//     }

//     bool Set(const std::string& group, const std::string& key, ByteView value) const {
//         last_group = group;
//         last_key = key;
//         received_value = value;
//         return should_succeed;
//     }

//     bool Delete(const std::string& group, const std::string& key) const {
//         last_group = group;
//         last_key = key;
//         return should_succeed;
//     }
// };

// class MockPeerPicker : public PeerPicker {
// public:
//     std::unique_ptr<MockPeer> mock_peer = std::make_unique<MockPeer>();

//     auto PickPeer(const std::string& key) const -> Peer* { return mock_peer.get(); }
// };

TEST_F(CacheGroupTest, RegisterPeerPicker) {
    CacheGroup group("test", 1024, getter_);
    auto peer_picker = std::make_unique<PeerPicker>("localhost:8001", "test_service");

    group.RegisterPeerPicker(std::move(peer_picker));

    // This test just ensures the method can be called without errors
    // The actual peer functionality would need integration testing
}

TEST_F(CacheGroupTest, SyncToPeersSet) {
    CacheGroup group("test", 1024, getter_);
    auto peer_picker = std::make_unique<PeerPicker>("localhost:8001", "test_service");

    group.RegisterPeerPicker(std::move(peer_picker));

    ByteView value("sync_value");
    group.SyncToPeers("sync_key", SyncFlag::SET, value);

    // This test ensures the method can be called
    // In a real implementation, you'd verify the peer was called
}

TEST_F(CacheGroupTest, SyncToPeersDelete) {
    CacheGroup group("test", 1024, getter_);
    auto peer_picker = std::make_unique<PeerPicker>("localhost:8001", "test_service");

    group.RegisterPeerPicker(std::move(peer_picker));

    group.SyncToPeers("delete_key", SyncFlag::DELETE, ByteView{""});

    // This test ensures the method can be called without errors
}

TEST_F(CacheGroupTest, MultipleOperations) {
    CacheGroup group("test", 1024, getter_);

    // Test multiple get operations
    for (const auto& [key, expected_value] : db_) {
        auto result = group.Get(key);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().ToString(), expected_value);
        EXPECT_EQ(call_count_[key], 1);

        // Second get should hit cache
        result = group.Get(key);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().ToString(), expected_value);
        EXPECT_EQ(call_count_[key], 1);  // Should not increment
    }
}

// Global function tests
TEST(CacheGroupGlobalTest, MakeCacheGroup) {
    std::unordered_map<std::string, std::string> db = {{"global_key", "global_value"}};

    auto getter = [&db](const std::string& key) -> ByteViewOptional {
        auto it = db.find(key);
        if (it != db.end()) {
            return ByteView{it->second};
        }
        return std::nullopt;
    };

    auto& group = MakeCacheGroup("global_test", 1024, getter);

    auto result = group.Get("global_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().ToString(), "global_value");
}

TEST(CacheGroupGlobalTest, GetCacheGroup) {
    // First create a group
    auto getter = [](const std::string& key) -> ByteViewOptional { return std::nullopt; };

    MakeCacheGroup("lookup_test", 512, getter);

    // Then try to get it
    auto* group = GetCacheGroup("lookup_test");
    ASSERT_NE(group, nullptr);

    // Try to get non-existent group
    auto* null_group = GetCacheGroup("nonexistent");
    EXPECT_EQ(null_group, nullptr);
}

TEST(CacheGroupGlobalTest, MultipleCacheGroups) {
    auto getter1 = [](const std::string& key) -> ByteViewOptional {
        if (key == "test1") return ByteView{"value1"};
        return std::nullopt;
    };

    auto getter2 = [](const std::string& key) -> ByteViewOptional {
        if (key == "test2") return ByteView{"value2"};
        return std::nullopt;
    };

    auto& group1 = MakeCacheGroup("multi_test1", 512, getter1);
    auto& group2 = MakeCacheGroup("multi_test2", 512, getter2);

    // Test that groups are separate
    auto result1 = group1.Get("test1");
    auto result2 = group2.Get("test2");

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result1.value().ToString(), "value1");
    EXPECT_EQ(result2.value().ToString(), "value2");

    // Cross-group access should fail
    auto cross_result1 = group1.Get("test2");
    auto cross_result2 = group2.Get("test1");

    EXPECT_FALSE(cross_result1.has_value());
    EXPECT_FALSE(cross_result2.has_value());
}
