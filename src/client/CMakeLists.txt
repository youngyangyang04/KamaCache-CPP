# 客户端 SDK 库
add_library(kcache_client_sdk STATIC "${CMAKE_CURRENT_SOURCE_DIR}/client_sdk.cpp")
# PUBLIC 让链接此库的目标也能访问这些头文件和依赖
target_include_directories(kcache_client_sdk PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(kcache_client_sdk PUBLIC kcache_core)
