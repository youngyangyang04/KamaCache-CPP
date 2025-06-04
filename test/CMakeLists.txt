enable_testing()

# 测试 lru 缓存
add_executable(test_lru
    "./test_lru.cpp"
    "../src/cache/lru.cpp"
)
target_link_libraries(test_lru GTest::gtest_main)
