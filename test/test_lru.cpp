#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "kcache/cache.h"

TEST(LRUCacheTest, TestGet) {
    kcache::LRUCache cache{100, nullptr};
    auto ret = cache.Get("1");
    EXPECT_EQ(ret, std::nullopt);

    cache.Set("abcdefg", kcache::ByteView{"abcdefg"});
    ret = cache.Get("abcdefg");
    EXPECT_NE(ret, std::nullopt);
    EXPECT_EQ(ret.value().ToString(), "abcdefg");

    cache.Set("11", kcache::ByteView{"22"});
    ret = cache.Get("11");
    EXPECT_NE(ret, std::nullopt);
    EXPECT_EQ(ret.value().ToString(), "22");

    cache.Set("123456789", kcache::ByteView{"123456789"});
    ret = cache.Get("123456789");
    EXPECT_NE(ret, std::nullopt);
    EXPECT_EQ(ret.value().ToString(), "123456789");
}

TEST(LRUCacheTest, TestRemoveOldest) {
    kcache::LRUCache cache{40, nullptr};
    cache.Set("12345", kcache::ByteView{"abcde"});
    cache.Set("67890", kcache::ByteView{"fghij"});
    cache.Set("xxxxx", kcache::ByteView{"11111"});
    cache.Set("yyyyy", kcache::ByteView{"22222"});

    // 这个时候应该已经满了
    // 再加入新的缓存，原来最旧的缓存 {"12345", "abcde"}会被淘汰
    cache.Set("zzzzz", kcache::ByteView{"33333"});

    auto ret = cache.Get("12345");
    EXPECT_EQ(ret, std::nullopt);

    ret = cache.Get("67890");
    EXPECT_EQ(ret.value().ToString(), "fghij");
}

TEST(LRUCacheTest, TestEvictedFunc) {
    std::vector<kcache::Entry> kvs;
    auto evicted_func = [&](std::string key, const kcache::ByteView& value) {
        std::cout << "test evicted function...\n";
        kvs.emplace_back(kcache::Entry{key, value});
    };
    kcache::LRUCache cache{10, evicted_func};

    // 容量只有10，也就是在完成下面四次插入后，key1 和 k2 会被淘汰
    cache.Set("key1", kcache::ByteView{"123456"});
    cache.Set("k2", kcache::ByteView{"v2"});
    cache.Set("k3", kcache::ByteView{"v3"});
    cache.Set("k4", kcache::ByteView{"v4"});

    std::vector<kcache::Entry> expected{{"key1", kcache::ByteView{"123456"}}, {"k2", kcache::ByteView{"v2"}}};
    EXPECT_EQ(kvs, expected);
}