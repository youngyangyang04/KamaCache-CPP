#include "kcache/peer.h"

#include <fmt/base.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <optional>

#include "kcache.grpc.pb.h"
#include "kcache.pb.h"
#include "kcache/cache.h"

namespace kcache {

Peer::Peer(const std::string& addr, const std::string& service_name, std::shared_ptr<etcd::Client> etcd_cli)
    : addr_(addr), service_name_(service_name), etcd_client_(etcd_cli) {
    if (etcd_client_ == nullptr) {
        etcd_client_ = std::make_unique<etcd::Client>("http://127.0.0.1:2379");
    }

    // gRPC 连接选项
    grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 100);
    args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000);

    auto channel = grpc::CreateCustomChannel(addr, grpc::InsecureChannelCredentials(), args);
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (!channel->WaitForConnected(deadline)) {
        spdlog::error("Failed to dial server because connection timed out");
    }

    auto grpc_cli = pb::KCache::NewStub(channel);

    channel_ = std::move(channel);
    grpc_client_ = std::move(grpc_cli);
}

auto Peer::Get(const std::string& group_name, const std::string& key) -> ByteViewOptional {
    grpc::ClientContext ctx;
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);
    ctx.set_deadline(deadline);

    pb::Request request;
    request.set_group(group_name);
    request.set_key(key);

    pb::GetResponse response;
    grpc::Status status = grpc_client_->Get(&ctx, request, &response);

    if (!status.ok()) {
        return std::nullopt;
    }
    const std::string& data = response.value();
    return ByteView{data};
}

bool Peer::Set(const std::string& group_name, const std::string& key, ByteView value) {
    grpc::ClientContext context;
    // 假设 Set 操作没有硬性超时要求，或者由调用方 context 决定

    kcache::pb::Request request;  // 使用正确的命名空间
    request.set_group(group_name);
    request.set_key(key);
    request.set_value(value.data_.data(), value.data_.size());  // 从 std::vector<uint8_t> 设置字节数据

    kcache::pb::SetResponse response;  // Set 方法返回 GetResponse
    grpc::Status status = grpc_client_->Set(&context, request, &response);

    if (!status.ok()) {
        spdlog::error("Failed to set key to kcache: {}", status.error_message());
        return false;
    }
    return true;
}

bool Peer::Delete(const std::string& group_name, const std::string& key) {
    grpc::ClientContext context;
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);
    context.set_deadline(deadline);

    pb::Request request;  // 使用正确的命名空间
    request.set_group(group_name);
    request.set_key(key);

    pb::DeleteResponse response;  // 使用正确的命名空间和消息类型
    grpc::Status status = grpc_client_->Delete(&context, request, &response);

    if (!status.ok()) {
        spdlog::error("Failed to delete key from kcache: {}", status.error_message());
        return false;
    }
    return true;
}

}  // namespace kcache