# KamaCache-CPP

【代码随想录知识星球】项目分享-分布式缓存项目

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/kerolt/kcache)

KCache 是一个分布式缓存系统，基于一致性哈希算法实现数据分片，确保负载均衡，采用 LRU（最近最少使用）缓存淘汰算法；使用 gRPC 进行节点间高效通信，并基于 etcd 实现服务注册与发现，实时监控集群状态变化。该项目使用 conan 作为包管理工具，使用 CMake 作为项目的构建工具。

## 运行环境

- Ubuntu 22.04 (Docker)
- GCC 11.4 (use C++ 17)
- CMake 3.22.1
- Conan 2.16.1

## 项目依赖

- gflags / 2.2.2
- gtest / 1.16.0
- protobuf / 3.21.12
- grpc / 1.54.3
- etcd-cpp-apiv3 / 0.15.4
- fmt / 11.1.3
- spdlog / 1.15.1

该项目使用 [conan](https://conan.io/) 作为依赖管理工具，在配置该项目时需确保当前系统上安装了 conan。

> 项目 v2 版本不使用 vcpkg 了是因为其无法下载 etcd-cpp-apiv3，为了方便管理第三方库就只使用了 conan。

## 数据流程

当有客户端请求 kcache node 中的数据时：

1. 本地查找: 首先检查本地 LRU 缓存
2. 节点路由: 使用一致性哈希确定负责节点
3. 远程获取: 通过 gRPC 向目标节点请求数据
4. 数据源回退: 如果缓存未命中，从原始数据源加载
5. 缓存更新: 将数据存储到本地缓存以供后续使用

示意架构图如下：

![](https://obsidian-image-oss.oss-cn-shanghai.aliyuncs.com/undefined20250606-144135.png)

## 致谢

1. 本项目参考了 [geektutu/7days-golang](https://github.com/geektutu/7days-golang) 项目，感谢其作者提供的 [教程](https://geektutu.com/post/geecache.html) 和代码示例。
2. 项目第二版参考了[【代码随想录知识星球】项目分享-缓存系统（Go）](https://github.com/youngyangyang04/KamaCache-Go)

## 项目结构

```
.
|-- .vscode
|   `-- launch.json
|-- example
|   |-- CMakeLists.txt
|   `-- example.cpp
|-- src
|   |-- cache
|   |   `-- lru.cpp
|   |-- consistent_hash
|   |   `-- consistent_hash.cpp
|   |-- group
|   |   `-- group.cpp
|   |-- include
|   |   `-- kcache
|   |       |-- cache.h
|   |       |-- consistent_hash.h
|   |       |-- group.h
|   |       |-- grpc_server.h
|   |       |-- peer.h
|   |       |-- registry.h
|   |       `-- singleflight.h
|   |-- peer
|   |   |-- peer.cpp
|   |   `-- peer_picker.cpp
|   |-- proto
|   |   |-- kcache.grpc.pb.cc
|   |   |-- kcache.grpc.pb.h
|   |   |-- kcache.pb.cc
|   |   |-- kcache.pb.h
|   |   `-- kcache.proto
|   |-- registry
|   |   `-- registry.cpp
|   |-- server
|   |   `-- grpc_server.cpp
|   `-- CMakeLists.txt
|-- test
|   |-- CMakeLists.txt
|   |-- test_consistent_hash.cpp
|   |-- test_group.cpp
|   `-- test_lru.cpp
|-- .clang-format
|-- .gitignore
|-- CMakeLists.txt
|-- CMakePresets.json
|-- conanfile.txt
|-- LICENSE
`-- README.md
```

## 构建和运行

### 使用 CMake 构建

1. 使用 conan 的 CMake 配置:

    在项目根目录下执行：

    ```sh
    conan install . --build=missing -s build_type=<Debug|Release>
    ```

    Debug 和 Release 取决于你的选择。

2. 配置项目：

    ```sh
    # cmake >= 3.23 
    cmake --preset conan-debug
    # cmake < 3.23
    cmake <path> -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=generators/conan_toolchain.cmake  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Debug
    ```

    可按照你的需求将命令中的 debug(Debug) 换成 release(Release)

3. 构建项目：

    ```sh
    cmake --build build [--target <target>]
    ```

完成后，在 `src/proto` 目录下会生成 `src/proto/kcache.proto` 相关的 pb 和 grpc 文件：

- kcache.grpc.pb.cc
- kcache.grpc.pb.h
- kcache.pb.cc
- kcache.pb.h

### 运行

`example/example.cpp` 是一个使用 kcache 的示例代码，当完成项目的构建编译后，将会在 `build/example` 下生成可执行文件 `example`。

分别在三个终端运行以下命令：

```sh
# 节点 A
./build/example/example --port=8001 --node=A
```

```sh
# 节点 B
./build/example/example --port=8002 --node=B
```

```sh
# 节点 C
./build/example/example --port=8003 --node=C
```

## 核心组件

### LRU Cache 本地缓存

`Cache` 组件管理本地存储的缓存数据，底层采用 LRU 算法

- 提供线程安全的缓存访问
- 管理统计信息（命中、未命中）
- 处理缓存过期

### Group 缓存组

`Group` 是缓存的逻辑命名空间，**是缓存操作的主要接口**。外部可通过 rpc 来使用缓存节点，Group 会去执行对应的操作。

主要职责：

- 管理缓存数据的逻辑命名空间
- 协调本地缓存和远程节点之间的访问
- 处理缓存操作（Get、Set、Delete）
- 使用 singleflight 模式防止缓存击穿

### 一致性哈希

使用一致性哈希确定哪个节点负责哪个键，确保键值的均匀分布，并在添加或移除节点时最小化重新分布。

### PeerPicker 节点选择器

节点管理系统使节点能够相互通信，使用一致性哈希分布键值，并监听 etcd 的键值对变化。

### gRPC Server

每个节点都会跑一个 gRPC server，每个节点既是一个客户端也是服务端。

- 处理其他节点的请求（其他节点在没有缓存某些键的时候会访问含有这个键的节点）
- 向服务注册中心（etcd）注册自己
- 管理服务器生命周期

### 服务注册与发现

项目使用 etcd 进行服务发现。每个节点启动时向 etcd 注册自己（通过 gRPC Server），同时节点通过监听 etcd 变化发现可用的 peer 节点。

## 许可证

该项目使用 MIT 许可证，详情见 [LICENSE](LICENSE) 文件。
