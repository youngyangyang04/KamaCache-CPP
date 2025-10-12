// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "kcache/cache.h"
#include "kcache/group.h"

using namespace kcache;

class CacheGroupTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = {{"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"}};
        call_count_.clear();

        getter_ = [this](const std::string& key) -> ByteViewOptional {
            // 统计回源调用次数
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

// 首次回源成功后，二次 Get 命中本地缓存，不再调用 getter
TEST_F(CacheGroupTest, GetCachesOnHit) {
    CacheGroup group("group_basic", 1024, getter_);

    auto r1 = group.Get("key1");
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);

    // 第二次应命中本地缓存，不再回源
    auto r2 = group.Get("key1");
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);
}

// 不存在的 key 返回空值（nullopt）
TEST_F(CacheGroupTest, GetReturnsNulloptForMissing) {
    CacheGroup group("group_missing", 1024, getter_);
    auto r = group.Get("not_exist");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(call_count_["not_exist"], 1);
}

// 先 Set 再 Get，不触发 getter
TEST_F(CacheGroupTest, SetThenGetDoesNotCallGetter) {
    CacheGroup group("group_set", 1024, getter_);

    EXPECT_TRUE(group.Set("manual", ByteView{"v"}));
    auto r = group.Get("manual");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->ToString(), "v");
    EXPECT_EQ(call_count_["manual"], 0);
}

// 删除后再次 Get 会触发一次回源
TEST_F(CacheGroupTest, DeleteRemovesCache) {
    CacheGroup group("group_delete", 1024, getter_);

    ASSERT_TRUE(group.Get("key2").has_value());
    EXPECT_TRUE(group.Delete("key2"));

    call_count_["key2"] = 0;  // 重置统计
    auto r = group.Get("key2");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->ToString(), "value2");
    EXPECT_EQ(call_count_["key2"], 1);
}

// 调用 InvalidateFromPeer 仅删除本地缓存，下一次 Get 才会回源
TEST_F(CacheGroupTest, InvalidateFromPeerOnlyDeletesLocal) {
    CacheGroup group("group_invalidate", 1024, getter_);

    ASSERT_TRUE(group.Get("key3").has_value());
    // 模拟来自其他节点的失效
    EXPECT_TRUE(group.InvalidateFromPeer("key3"));

    call_count_["key3"] = 0;
    auto r = group.Get("key3");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->ToString(), "value3");
    // 失效后应回源一次
    EXPECT_EQ(call_count_["key3"], 1);
}

// 空 key 在 Get/Set/Delete/InvalidateFromPeer 均返回 false
TEST_F(CacheGroupTest, EmptyKeyIsRejected) {
    CacheGroup group("group_empty", 1024, getter_);

    EXPECT_FALSE(group.Get("").has_value());
    EXPECT_FALSE(group.Set("", ByteView{"x"}));
    EXPECT_FALSE(group.Delete(""));
    EXPECT_FALSE(group.InvalidateFromPeer(""));
}

// 多线程并发对同一 key 的 Get，只回源一次，其它线程复用结果
TEST_F(CacheGroupTest, SingleFlightAvoidsDuplicateLoads) {
    // getter 增加少量延迟，放大并发窗口
    DataGetter slow_getter = [this](const std::string& key) -> ByteViewOptional {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        call_count_[key]++;
        auto it = db_.find(key);
        if (it != db_.end()) return ByteView{it->second};
        return std::nullopt;
    };

    CacheGroup group("group_sf", 1024, slow_getter);

    const int N = 16;
    std::vector<std::thread> ths;
    ths.reserve(N);
    std::vector<bool> ok(N, false);

    for (int i = 0; i < N; ++i) {
        ths.emplace_back([&, i] {
            auto r = group.Get("key1");
            ok[i] = r.has_value() && r->ToString() == "value1";
        });
    }
    for (auto& t : ths) t.join();

    for (int i = 0; i < N; ++i) EXPECT_TRUE(ok[i]);
    // 并发同 key 只应回源一次
    EXPECT_EQ(call_count_["key1"], 1);
}

// 移动构造后，已缓存的值仍可直接命中，不重复回源
TEST_F(CacheGroupTest, MoveConstructorPreservesCache) {
    CacheGroup g1("group_move_ctor", 1024, getter_);
    ASSERT_TRUE(g1.Get("key1").has_value());

    CacheGroup g2(std::move(g1));
    auto r = g2.Get("key1");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->ToString(), "value1");
    EXPECT_EQ(call_count_["key1"], 1);  // 未再次回源
}

// 移动赋值后亦保持缓存命中
TEST_F(CacheGroupTest, MoveAssignmentPreservesCache) {
    CacheGroup g1("group_move_assign_src", 1024, getter_);
    CacheGroup g2("group_move_assign_dst", 1024, getter_);
    ASSERT_TRUE(g1.Get("key2").has_value());

    g2 = std::move(g1);

    auto r = g2.Get("key2");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->ToString(), "value2");
    EXPECT_EQ(call_count_["key2"], 1);
}

TEST_F(CacheGroupTest, BatchGetAcrossKeys) {
    CacheGroup group("group_batch", 1024, getter_);
    for (const auto& kv : db_) {
        auto r1 = group.Get(kv.first);
        ASSERT_TRUE(r1.has_value());
        EXPECT_EQ(r1->ToString(), kv.second);
        auto r2 = group.Get(kv.first);
        ASSERT_TRUE(r2.has_value());
        EXPECT_EQ(r2->ToString(), kv.second);
        EXPECT_EQ(call_count_[kv.first], 1);
    }
}

// 全局方法测试
TEST(CacheGroupGlobalTest, MakeCacheGroupCreatesUsableGroup) {
    std::unordered_map<std::string, std::string> db = {{"gkey", "gvalue"}};
    auto getter = [&db](const std::string& key) -> ByteViewOptional {
        auto it = db.find(key);
        if (it != db.end()) return ByteView{it->second};
        return std::nullopt;
    };

    auto& group = MakeCacheGroup("global_test_group", 1024, getter);
    auto r = group.Get("gkey");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->ToString(), "gvalue");
}

TEST(CacheGroupGlobalTest, GetCacheGroupLookup) {
    auto getter = [](const std::string&) -> ByteViewOptional { return std::nullopt; };
    MakeCacheGroup("lookup_group", 512, getter);

    auto* found = GetCacheGroup("lookup_group");
    ASSERT_NE(found, nullptr);

    auto* not_found = GetCacheGroup("not_exist_group");
    EXPECT_EQ(not_found, nullptr);
}

// 不同名称的 group 相互独立，交叉访问失败
TEST(CacheGroupGlobalTest, MultipleNamedGroupsAreIndependent) {
    auto getter1 = [](const std::string& key) -> ByteViewOptional {
        if (key == "k1") return ByteView{"v1"};
        return std::nullopt;
    };
    auto getter2 = [](const std::string& key) -> ByteViewOptional {
        if (key == "k2") return ByteView{"v2"};
        return std::nullopt;
    };

    auto& g1 = MakeCacheGroup("g1", 256, getter1);
    auto& g2 = MakeCacheGroup("g2", 256, getter2);

    auto r1 = g1.Get("k1");
    auto r2 = g2.Get("k2");
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->ToString(), "v1");
    EXPECT_EQ(r2->ToString(), "v2");

    // 交叉访问应失败
    EXPECT_FALSE(g1.Get("k2").has_value());
    EXPECT_FALSE(g2.Get("k1").has_value());
}
