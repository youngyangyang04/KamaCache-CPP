#include "kcache/grpc_server.h"

#include <fmt/base.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h>

#include <memory>

#include "kcache/group.h"

namespace kcache {

CacheGrpcServer::CacheGrpcServer(const std::string& addr, const std::string& svc_name)
    : addr_(addr), svc_name_(svc_name) {
    // 创建etcd注册器
    etcd_register_ = std::make_unique<EtcdRegistry>();
    if (!etcd_register_->Register(svc_name_, addr_)) {
        throw std::runtime_error("[kcache] Failed to register service with etcd");
    }
}

auto CacheGrpcServer::Get(grpc::ServerContext* context, const pb::Request* request, pb::GetResponse* response)
    -> grpc::Status {
    auto group = GetCacheGroup(request->group());
    if (!group) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Group not found");
    }
    auto value = group->Get(request->key());
    if (!value) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Key not found");
    }
    response->set_value(value->ToString());
    return grpc::Status::OK;
}

auto CacheGrpcServer::Set(grpc::ServerContext* context, const pb::Request* request, pb::SetResponse* response)
    -> grpc::Status {
    auto group = GetCacheGroup(request->group());
    if (!group) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Group not found");
    }
    bool is_gateway = context->client_metadata().find("is_gateway") != context->client_metadata().end();
    bool is_from_peer = !is_gateway;
    bool is_set = group->Set(request->key(), request->value(), is_from_peer);
    response->set_value(is_set);
    return grpc::Status::OK;
}

auto CacheGrpcServer::Delete(grpc::ServerContext* context, const pb::Request* request, pb::DeleteResponse* response)
    -> grpc::Status {
    auto group = GetCacheGroup(request->group());
    if (!group) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Group not found");
    }
    bool is_gateway = context->client_metadata().find("is_gateway") != context->client_metadata().end();
    bool is_from_peer = !is_gateway;
    bool is_delete = group->Delete(request->key(), is_from_peer);
    response->set_value(is_delete);
    return grpc::Status::OK;
}

// 启动 gRPC 服务器
void CacheGrpcServer::Start() {
    try {
        // 配置gRPC服务器选项
        grpc::ServerBuilder builder;

        // 设置最大消息大小
        builder.SetMaxReceiveMessageSize(opts_.max_msg_size);
        builder.SetMaxSendMessageSize(opts_.max_msg_size);

        // 配置TLS或非安全连接
        if (opts_.tls) {
            auto creds = LoadTLSCredentials(opts_.cert_file, opts_.key_file);
            if (!creds) {
                throw std::runtime_error("Failed to load TLS credentials");
            }
            builder.AddListeningPort(addr_, creds);
        } else {
            builder.AddListeningPort(addr_, grpc::InsecureServerCredentials());
        }

        // 启用默认健康检查服务
        grpc::EnableDefaultHealthCheckService(true);
        builder.SetOption(grpc::MakeChannelArgumentOption(GRPC_ARG_KEEPALIVE_TIME_MS, 30000));
        builder.SetOption(grpc::MakeChannelArgumentOption(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000));

        // 注册服务
        builder.RegisterService(this);

        // 构建并启动服务器
        grpc_server_ = builder.BuildAndStart();
        if (!grpc_server_) {
            throw std::runtime_error("Failed to build and start gRPC server");
        }

        // 设置健康检查状态
        auto health_service = grpc_server_->GetHealthCheckService();
        if (health_service) {
            health_service->SetServingStatus(svc_name_, true);
        }

        is_stop_ = false;

        spdlog::info("gRPC Server start success at {}!", addr_);

        grpc_server_->Wait();

    } catch (const std::exception& e) {
        spdlog::error("Failed to start gRPC Server: {}", e.what());
        throw;
    }
}

// 关闭 gRPC 服务器
void CacheGrpcServer::Stop() {
    is_stop_ = true;
    if (etcd_register_) {
        etcd_register_->Unregister();
        etcd_register_.reset();
    }
    if (grpc_server_) {
        grpc_server_->Shutdown();
        grpc_server_.reset();
    }
    spdlog::info("gRPC Server {} stopped.", addr_);
}

// TODO 实现加载 TLS 证书
auto CacheGrpcServer::LoadTLSCredentials(const std::string& cert_file, const std::string& key_file)
    -> std::shared_ptr<grpc::ServerCredentials> {
    return grpc::SslServerCredentials(grpc::SslServerCredentialsOptions{});
}

}  // namespace kcache