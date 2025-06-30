# 使用 Ubuntu 22.04 作为基础镜像
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gcc \
    g++ \
    git \
    wget \
    curl \
    python3 \
    python3-pip \
    pkg-config

# 安装 Conan 包管理器
RUN pip3 install --upgrade pip && pip3 install conan==2.16.1

# 创建工作目录
WORKDIR /app

# 复制项目文件
COPY . .

# 创建 Conan profile 并检测系统设置
RUN conan profile detect --force

# 安装依赖并生成构建文件
RUN conan install . --build=missing -s build_type=Release

# 配置 CMake 项目
RUN cmake -DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -S . -B build -G Ninja

# 构建项目
RUN cmake --build build

# 创建运行目录
RUN mkdir -p /app/bin && \
    cp build/bin/kgateway /app/bin/ && \
    cp build/bin/knode /app/bin/
