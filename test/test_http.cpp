#include <format>
#include <iostream>

#include "cache.h"
#include "http.h"

int main() {
    std::unordered_map<std::string, std::string> db = {{"Tom", "400"}, {"Kerolt", "370"}, {"Jack", "296"}};
    auto& cache =
        kcache::NewCacheGroup("score", 2 << 10, [&](const std::string& key) -> std::optional<kcache::ValueRef> {
            std::cout << std::format("[DB] search key: {}\n", key);
            if (db.contains(key)) {
                return std::make_shared<kcache::ByteValue>(db[key]);
            }
            return std::nullopt;
        });

    kcache::HTTPPool srv{};
    srv.Start();
    std::cout << std::format("Cache is running at {}:{}\n", srv.GetHost(), srv.GetPort());
}