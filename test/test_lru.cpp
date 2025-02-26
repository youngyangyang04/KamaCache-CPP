#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lru.h"

struct StringValue : kcache::Value {
    std::string data_;

    StringValue(const std::string& s) : data_(s) {}
    StringValue(const char* s) : data_(s) {}

    auto Len() -> int64_t override { return data_.size(); }
    auto ToString() -> std::string override { return data_; };
};

TEST(LRUCacheTest, TestGet) {
    kcache::LRUCache cache{100, nullptr};
    auto ret = cache.Get("1");
    EXPECT_EQ(ret, std::nullopt);

    cache.Put("abcdefg", std::make_shared<StringValue>("abcdefg"));
    ret = cache.Get("abcdefg");
    EXPECT_NE(ret, std::nullopt);
    EXPECT_EQ(ret.value()->ToString(), "abcdefg");

    cache.Put("11", std::make_shared<StringValue>("22"));
    ret = cache.Get("11");
    EXPECT_NE(ret, std::nullopt);
    EXPECT_EQ(ret.value()->ToString(), "22");

    cache.Put("123456789", std::make_shared<StringValue>("123456789"));
    ret = cache.Get("123456789");
    EXPECT_NE(ret, std::nullopt);
    EXPECT_EQ(ret.value()->ToString(), "123456789");
}

TEST(LRUCacheTest, TestRemoveOldest) {
    kcache::LRUCache cache{40, nullptr};
    cache.Put("12345", std::make_shared<StringValue>("abcde"));
    cache.Put("67890", std::make_shared<StringValue>("fghij"));
    cache.Put("xxxxx", std::make_shared<StringValue>("11111"));
    cache.Put("yyyyy", std::make_shared<StringValue>("22222"));

    // 这个时候应该已经满了
    // 再加入新的缓存，原来最旧的缓存 {"12345", "abcde"}会被淘汰
    cache.Put("zzzzz", std::make_shared<StringValue>("33333"));

    auto ret = cache.Get("12345");
    EXPECT_EQ(ret, std::nullopt);

    ret = cache.Get("67890");
    EXPECT_EQ(ret.value()->ToString(), "fghij");
}

TEST(LRUCacheTest, TestEvictedFunc) {
    std::vector<kcache::Entry> kvs;
    auto evicted_func = [&](std::string key, kcache::ValueRef value) {
        std::cout << "test evicted function...\n";
        kvs.emplace_back(key, value);
    };
    kcache::LRUCache cache{10, evicted_func};

    // 容量只有10，也就是在完成下面四次插入后，key1 和 k2 会被淘汰
    cache.Put("key1", std::make_shared<StringValue>("123456"));
    cache.Put("k2", std::make_shared<StringValue>("v2"));
    cache.Put("k3", std::make_shared<StringValue>("v3"));
    cache.Put("k4", std::make_shared<StringValue>("v4"));

    std::vector<kcache::Entry> expected{{"key1", std::make_shared<StringValue>("123456")},
                                        {"k2", std::make_shared<StringValue>("v2")}};
    EXPECT_EQ(kvs, expected);
}