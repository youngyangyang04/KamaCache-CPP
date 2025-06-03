cmake_minimum_required(VERSION 3.20)
project(KCache)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GFLAGS_USE_TARGET_NAMESPACE ON)

# 头文件位置：
# - src/include 项目通用头文件
# - src/proto 生成的 proto 和 grpc 文件头文件
include_directories(${CMAKE_SOURCE_DIR}/src/include ${CMAKE_SOURCE_DIR}/src/proto)

set(protobuf_MODULE_COMPATIBLE TRUE)

# 查找依赖
find_package(GTest CONFIG REQUIRED)
find_package(gflags CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(etcd-cpp-apiv3 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(example)