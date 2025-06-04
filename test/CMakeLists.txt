enable_testing()

# 测试 lru 缓存
add_executable(test_lru
    "./test_lru.cpp"
    "../src/cache/lru.cpp"
)
target_link_libraries(test_lru GTest::gtest_main)

# 测试一致性哈希
add_executable(test_cons_hash
    "./test_consistent_hash.cpp"
    "../src/consistent_hash/consistent_hash.cpp"
)
target_link_libraries(test_cons_hash GTest::gtest_main fmt::fmt)
