if(CMAKE_CROSSCOMPILING)
    find_program(PROTOC_EXE protoc)
    find_program(GRPC_CPP_PLUGIN_EXE grpc_cpp_plugin)
else()
    set(PROTOC_EXE $<TARGET_FILE:protobuf::protoc>)
    set(GRPC_CPP_PLUGIN_EXE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

# 定义生成的文件路径和proto文件的位置
set(PROTO_FILE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_FILE_NAME "${PROTO_FILE_DIR}/kcache.proto")
file(MAKE_DIRECTORY ${PROTO_FILE_DIR}) # 创建 PROTO_FILE_DIR 目录
set(PROTO_CPP_FILE ${PROTO_FILE_DIR}/kcache.pb.cc)
set(PROTO_HDR_FILE ${PROTO_FILE_DIR}/kcache.pb.h)
set(GRPC_CPP_FILE ${PROTO_FILE_DIR}/kcache.grpc.pb.cc) # gRPC 生成的 .cc
set(GRPC_HDR_FILE ${PROTO_FILE_DIR}/kcache.grpc.pb.h)   # gRPC 生成的 .h
get_filename_component(PROTO_FILE "${PROTO_FILE_DIR}/kcache.proto" ABSOLUTE)
get_filename_component(PROTO_FILE_PATH "${PROTO_FILE}" PATH)

# 生成 *.pb.cc *.pb.h *.grpc.pb.h *.grpc.pb.cc
add_custom_command(
    COMMAND ${PROTOC_EXE}
        ARGS --grpc_out ${PROTO_FILE_DIR} --cpp_out ${PROTO_FILE_DIR} 
             -I ${PROTO_FILE_PATH} --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_EXE}" "${PROTO_FILE}"
    OUTPUT ${PROTO_CPP_FILE} ${PROTO_HDR_FILE}
            ${GRPC_CPP_FILE} ${GRPC_HDR_FILE}
    DEPENDS ${PROTO_FILE})

# 将生成的 proto 相关的 cpp 文件构建成一个库
add_library(kcache_proto STATIC ${PROTO_CPP_FILE} ${PROTO_HDR_FILE}
                        ${GRPC_CPP_FILE} ${GRPC_HDR_FILE})
target_link_libraries(kcache_proto protobuf::libprotobuf grpc::grpc)

#生成 kcache 目标
file(GLOB_RECURSE KCACHE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(kcache STATIC ${KCACHE_SRC})
target_include_directories(kcache PRIVATE "./src" ${PROTO_FILE_DIR})
target_link_libraries(kcache kcache_proto protobuf::libprotobuf etcd-cpp-apiv3::etcd-cpp-apiv3 fmt::fmt grpc::grpc spdlog::spdlog)