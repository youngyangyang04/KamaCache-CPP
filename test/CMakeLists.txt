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

# 测试 cache group
add_executable(test_group
    "./test_group.cpp"
    "../src/cache/lru.cpp"
    "../src/consistent_hash/consistent_hash.cpp"
    "../src/group/group.cpp"
    "../src/peer/peer.cpp"
    "../src/peer/peer_picker.cpp"
)
target_link_libraries(test_group
    GTest::gtest_main
    fmt::fmt
    grpc::grpc
    etcd-cpp-apiv3::etcd-cpp-apiv3
    spdlog::spdlog
    protobuf::protobuf
    kcache_proto
)
