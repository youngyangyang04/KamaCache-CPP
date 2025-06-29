#ifndef GRPC_SERVER_H_
#define GRPC_SERVER_H_

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <etcd/Client.hpp>

#include "kcache.grpc.pb.h"
#include "kcache.pb.h"
#include "kcache/registry.h"

namespace kcache {

struct ServerOptions {
    std::vector<std::string> etcd_endpoints;
    std::chrono::milliseconds dial_timeout;
    int max_msg_size;  // bytes
    bool tls;
    std::string cert_file;
    std::string key_file;

    // Default constructor to set default values
    ServerOptions()
        : etcd_endpoints({"localhost:2379"}),
          dial_timeout(std::chrono::seconds(5)),
          max_msg_size(4 << 20),  // 4MB
          tls(false) {}
};

// Function type for options
using ServerOption = std::function<void(ServerOptions*)>;

// Option functions
inline auto WithEtcdEndpoints(const std::vector<std::string>& endpoints) -> ServerOption {
    return [endpoints](ServerOptions* o) { o->etcd_endpoints = endpoints; };
}

inline auto WithDialTimeout(std::chrono::milliseconds timeout) -> ServerOption {
    return [timeout](ServerOptions* o) { o->dial_timeout = timeout; };
}

inline auto WithTLS(const std::string& certFile, const std::string& keyFile) -> ServerOption {
    return [certFile, keyFile](ServerOptions* o) {
        o->tls = true;
        o->cert_file = certFile;
        o->key_file = keyFile;
    };
}

class CacheGrpcServer final : public pb::KCache::Service {
public:
    CacheGrpcServer(const std::string& addr, const std::string& svc_name);
    ~CacheGrpcServer() = default;

    auto Get(grpc::ServerContext* context, const pb::Request* request, pb::GetResponse* response)
        -> grpc::Status override;

    auto Set(::grpc::ServerContext* context, const pb::Request* request, pb::SetResponse* response)
        -> grpc::Status override;

    auto Delete(grpc::ServerContext* context, const pb::Request* request, pb::DeleteResponse* response)
        -> grpc::Status override;

    void Start();

    void Stop();

private:
    // Helper for loading TLS credentials
    auto LoadTLSCredentials(const std::string& cert_file, const std::string& key_file)
        -> std::shared_ptr<grpc::ServerCredentials>;

private:
    std::string addr_;
    std::string svc_name_;

    std::unique_ptr<grpc::Server> grpc_server_;
    std::unique_ptr<EtcdRegistry> etcd_register_;

    std::atomic<bool> is_stop_;

    ServerOptions opts_;
};

}  // namespace kcache

#endif
