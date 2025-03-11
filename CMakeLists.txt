cmake_minimum_required(VERSION 3.15)
project(KCache)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(GTest REQUIRED)
find_package(httplib REQUIRED)
find_package(ZLIB REQUIRED)
find_package(gflags CONFIG REQUIRED)

set(GFLAGS_USE_TARGET_NAMESPACE ON)

# kcache
file(GLOB KCACHE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(kcache ${KCACHE_SOURCES})
target_link_libraries(kcache PRIVATE
    httplib::httplib
    ZLIB::ZLIB
    gflags::gflags
)
target_include_directories(kcache PRIVATE src)

# test_http
file(GLOB SRC_CPP CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM SRC_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_executable(test_http
    test/test_http.cpp
    ${SRC_CPP})
target_link_libraries(test_http PRIVATE
    GTest::gtest_main
    httplib::httplib
    ZLIB::ZLIB)
target_include_directories(test_http PRIVATE src)

# 自动为 test 目录下的 cpp 文件构建同名的测试
file(GLOB TEST_FILES CONFIGURE_DEPENDS "test/*.cpp")
enable_testing()

foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)

    # test_http 单独设置
    if(NOT test_name STREQUAL "test_http")
        add_executable(${test_name}
            ${test_file}
            ${SRC_CPP})
        target_link_libraries(${test_name} PRIVATE
            GTest::gtest_main
            httplib::httplib
            ZLIB::ZLIB)
        target_include_directories(${test_name} PRIVATE src)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
endforeach()