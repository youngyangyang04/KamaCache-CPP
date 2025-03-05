cmake_minimum_required(VERSION 3.15)
project(KCache)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(GTest REQUIRED)
find_package(httplib REQUIRED)
find_package(ZLIB REQUIRED)

# Main executable
file(GLOB KCache_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(kcache ${KCache_SOURCES})
target_link_libraries(kcache PRIVATE 
    httplib::httplib 
    ZLIB::ZLIB)
target_include_directories(kcache PRIVATE src)

enable_testing()

# Test_http executable
file(GLOB SRC_CPP CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM SRC_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_executable(test_http 
    test/test_http.cpp 
    ${SRC_CPP})
target_link_libraries(test_http PRIVATE 
    GTest::gtest_main 
    httplib::httplib 
    ZLIB::ZLIB)
target_include_directories(test_http PRIVATE src)

# Auto-create test targets
file(GLOB TEST_FILES CONFIGURE_DEPENDS "test/*.cpp")
foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)
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