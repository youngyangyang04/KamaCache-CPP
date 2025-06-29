cmake_minimum_required(VERSION 3.20)
project(KCache)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置可执行文件输出目录为 build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 头文件位置：
include_directories(${CMAKE_SOURCE_DIR}/src/include # src/include 项目通用头文件
                    ${CMAKE_SOURCE_DIR}/src/proto) # src/proto 生成的 proto 和 grpc

# 查找依赖
find_package(GTest CONFIG REQUIRED)
find_package(gflags CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(etcd-cpp-apiv3 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(src)
add_subdirectory(test)
