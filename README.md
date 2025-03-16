# KamaCache-CPP

【代码随想录知识星球】项目分享-分布式缓存项目

KCache 是一个分布式缓存系统，支持一致性哈希和LRU缓存淘汰策略。该项目使用C++编写，使用 vcpkg 作为包管理工具、CMake 作为项目的构建工具。

![alt text](https://obsidian-image-oss.oss-cn-shanghai.aliyuncs.com/kcache_architecture)

## 致谢

本项目参考了 [geektutu/7days-golang](https://github.com/geektutu/7days-golang) 项目，感谢其作者提供的 [教程](https://geektutu.com/post/geecache.html) 和代码示例。

## 项目结构

```
.
├── src
│   ├── proto
│   │   └── kcache.proto
│   ├── cache.cpp
│   ├── cache.h
│   ├── consistent_hash.h
│   ├── http.cpp
│   ├── http.h
│   ├── lru.cpp
│   ├── lru.h
│   ├── main.cpp
│   └── singleflight.h
├── test
│   ├── test_cache.cpp
│   ├── test_consistent_hash.cpp
│   ├── test_http.cpp
│   └── test_lru.cpp
├── .clang-format
├── .gitignore
├── CMakeLists.txt
├── CMakePresets.json
├── CMakeUserPresets.json
├── LICENSE
├── README.md
├── run_cache_cluster.sh
├── test_singleflight.sh
├── vcpkg-configuration.json
└── vcpkg.json
```

## 依赖

项目依赖如下：

- cpp-httplib
- gflags
- gtest
- zlib
- protobuf

依赖管理使用 `vcpkg`，具体配置见 [vcpkg.json](vcpkg.json) 和 [vcpkg-configuration.json](vcpkg-configuration.json)。

## 构建和运行

### 使用 CMake 构建

1. 设置 `CMakeUserPresets.json`:

    ```json
    {
        "version": 2,
        "configurePresets": [
            {
                "name": "default",
                "inherits": "vcpkg",
                "environment": {
                    "VCPKG_ROOT": "<path>/<to>/<your vcpkg dir>"
                }
            }
        ]
    }
    ```

2. 配置项目：

    ```sh
    cmake --preset default
    ```

    执行了这条命令后，将会在 build/proto_gen 目录下生成 [src/proto/kcache.proto](src/proto/kcache.proto) 相关的 pb.cc 和 pb.h 文件。

3. 构建项目：

    ```sh
    cmake --build build [--target kcache|test_cache|test_http|test_lru|test_consistent_hash]
    ```

### 运行缓存集群

使用脚本 `run_cache_cluster.sh` 运行缓存集群：

```sh
chmod +x ./run_cache_cluster.sh
./run_cache_cluster.sh
```

### 测试 SingleFlight

使用脚本 `test_singleflight.sh` 测试 SingleFlight：

```sh
chmod +x ./test_singleflight.sh
./test_singleflight.sh
```

## 代码结构

### 主程序

主程序位于 [src/main.cpp](src/main.cpp)，负责启动缓存服务器和API服务器。

### 缓存操作

缓存操作的相关代码位于 [src/cache.h](src/cache.h) 和 [src/cache.cpp](src/cache.cpp)。

### LRU 缓存

LRU 缓存实现位于 [src/lru.h](src/lru.h) 和 [src/lru.cpp](src/lru.cpp)。

### 一致性哈希

一致性哈希实现位于 [src/consistent_hash.h](src/consistent_hash.h)。

### HTTP 服务器

HTTP 服务器实现位于 [src/http.h](src/http.h) 和 [src/http.cpp](src/http.cpp)。

### 单元测试

单元测试位于 `test` 目录下，使用 Google Test 编写。测试文件包括：

- [test/test_cache.cpp](test/test_cache.cpp)
- [test/test_http.cpp](test/test_http.cpp)
- [test/test_lru.cpp](test/test_lru.cpp)
- [test/test_consistent_hash.cpp](test/test_consistent_hash.cpp)

## 代码格式化

项目使用 `.clang-format` 文件进行代码格式化，具体配置见 [.clang-format](.clang-format)。

## 许可证

该项目使用 MIT 许可证，详情见 [LICENSE](LICENSE) 文件。
