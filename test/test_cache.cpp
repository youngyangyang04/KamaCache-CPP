#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "cache.h"
#include "lru.h"

TEST(CacheTest, TestGet) {
    std::unordered_map<std::string, std::string> db = {{"Tom", "400"}, {"Kerolt", "370"}, {"Jack", "296"}};
    std::unordered_map<std::string, int> count;

    auto getter = [&](const std::string& key) -> std::optional<kcache::ValueRef> {
        std::cout << std::format("[DB] search key: {}\n", key);
        if (db.contains(key)) {
            if (!count.contains(key)) {
                count[key] = 0;
            }
            ++count[key];
            return std::make_shared<kcache::ByteValue>(db[key]);
        }
        return std::nullopt;
    };
    auto& cache = kcache::NewCacheGroup("score", 2 << 10, getter);

    for (const auto& [k, v] : db) {
        // 先利用 getter 从 db 中获取
        if (auto value = cache.Get(k); !value || value.value()->ToString() != v) {
            std::cout << "fail to get value from DB\n";
        }
        // 再次调用 Get，如果 count 计数大于 1，说明多次调用了 getter，缓存失效
        if (auto value = cache.Get(k); !value || count[k] > 1) {
            std::cout << std::format("cache {} miss\n", k);
        }
    }

    if (auto value = cache.Get("Unknown"); value) {
        std::cout << std::format("the value of unknow should be empty, but {} got", value.value()->ToString());
    }
}