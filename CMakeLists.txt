cmake_minimum_required(VERSION 3.23)
project(KCache)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GFLAGS_USE_TARGET_NAMESPACE ON)

# Find dependencies
find_package(GTest REQUIRED)
find_package(httplib REQUIRED)
find_package(ZLIB REQUIRED)
find_package(gflags CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# ------------------------------ 生成 proto 相关的文件 ------------------------------ #
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto_gen)
set(PROTO_GEN_FILES "")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR}) # 创建 PROTO_GEN_DIR 目录
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/proto/*.proto")

# 遍历proto文件列表，生成对应的.pb.cc和.pb.h文件
foreach(file ${PROTO_FILES})
    # 获取文件名（不含扩展名）
    get_filename_component(proto_name ${file} NAME_WE)
    get_filename_component(proto_file ${file} NAME)

    # 定义生成的文件路径
    set(pb_cc_file ${PROTO_GEN_DIR}/${proto_name}.pb.cc)
    set(pb_h_file ${PROTO_GEN_DIR}/${proto_name}.pb.h)

    # 在配置阶段生成 pb.cc 和 pb.h
    execute_process(
        COMMAND ${Protobuf_PROTOC_EXECUTABLE} --cpp_out=${PROTO_GEN_DIR}
                --proto_path=${PROTO_DIR} ${proto_file}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} # 可选：设置工作目录
        RESULT_VARIABLE result # 捕获命令执行结果
        OUTPUT_QUIET # 不输出命令的标准输出
        ERROR_VARIABLE error # 捕获错误输出
    )

    # 检查命令是否成功执行
    if(NOT result EQUAL 0)
        message(
            FATAL_ERROR
                "Failed to generate C++ code from ${proto_file}: ${error}")
    endif()

    # 将生成的文件添加到列表中
    list(APPEND PROTO_GEN_FILES ${pb_cc_file} ${pb_h_file})
endforeach()
# ------------------------------ 生成 proto 相关的文件 ------------------------------ #

# ---------------------------------- kcache ---------------------------------- #
file(GLOB KCACHE_SRC "src/*.cpp")
add_executable(kcache ${KCACHE_SRC} ${PROTO_GEN_FILES})
target_include_directories(kcache PRIVATE src ${PROTO_GEN_DIR})
target_link_libraries(kcache PRIVATE httplib::httplib ZLIB::ZLIB gflags::gflags
                                     protobuf::libprotobuf)
# ---------------------------------- kcache ---------------------------------- #

# --------------------------------- test_http -------------------------------- #
file(GLOB SRC_CPP CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM SRC_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_executable(test_http test/test_http.cpp ${SRC_CPP} ${PROTO_GEN_FILES})
target_link_libraries(test_http PRIVATE GTest::gtest_main httplib::httplib
                                        ZLIB::ZLIB protobuf::libprotobuf)
target_include_directories(test_http PRIVATE src ${PROTO_GEN_DIR})
# --------------------------------- test_http -------------------------------- #

# ---------------------------------- 其他单元测试 ---------------------------------- #
# 自动为 test 目录下的 cpp 文件构建同名的测试
file(GLOB TEST_FILES CONFIGURE_DEPENDS "test/*.cpp")
enable_testing()

foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)

    # test_http 单独设置
    if(NOT test_name STREQUAL "test_http")
        add_executable(${test_name} ${test_file} ${SRC_CPP} ${PROTO_GEN_FILES})
        target_link_libraries(
            ${test_name} PRIVATE GTest::gtest_main httplib::httplib ZLIB::ZLIB  
                                 protobuf::libprotobuf)
        target_include_directories(${test_name} PRIVATE src ${PROTO_GEN_DIR})
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
endforeach()
# ---------------------------------- 其他单元测试 ---------------------------------- #
