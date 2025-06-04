add_executable(example
    example.cpp
)

target_include_directories(example
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/proto
)

target_link_libraries(example
    kcache
    gflags::gflags
)