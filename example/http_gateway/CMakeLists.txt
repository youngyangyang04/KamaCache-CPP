# http 网关示例
add_executable(http_gateway "${CMAKE_CURRENT_SOURCE_DIR}/http_gateway.cpp")
target_link_libraries(http_gateway PRIVATE 
    kcache_client_sdk
    gflags::gflags
    httplib::httplib
    nlohmann_json::nlohmann_json)
