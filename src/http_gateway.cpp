#include <gflags/gflags.h>
#include <grpcpp/grpcpp.h>
#include <httplib.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <etcd/Client.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "kcache.grpc.pb.h"
#include "kcache/consistent_hash.h"

DEFINE_int32(http_port, 9000, "HTTP服务端口");
DEFINE_string(etcd_endpoints, "http://127.0.0.1:2379", "etcd地址");
DEFINE_string(service_name, "kcache", "缓存服务名称");

using namespace kcache;

class HttpGateway {
public:
    HttpGateway(int port, const std::string& etcd_addr, const std::string& svc_name)
        : port_(port), etcd_addr_(etcd_addr), service_name_(svc_name) {
        etcd_client_ = std::make_shared<etcd::Client>(etcd_addr_);
        server_.set_payload_max_length(4 << 20);
        SetupRoutes();
        StartServiceDiscovery();
    }

    ~HttpGateway() {
        if (discovery_thread_.joinable()) {
            discovery_thread_.join();
        }
        server_.stop();
    }

    void Start() {
        spdlog::info("Starting HTTP Gateway on port {}", port_);
        server_.listen("0.0.0.0", port_);
    }

private:
    void SetupRoutes() {
        // GET /api/cache/{group}/{key}
        server_.Get(R"(/api/cache/([^/]+)/([^/]+))",
                    [this](const httplib::Request& req, httplib::Response& res) { HandleGet(req, res); });

        // POST /api/cache/{group}/{key}
        server_.Post(R"(/api/cache/([^/]+)/([^/]+))",
                     [this](const httplib::Request& req, httplib::Response& res) { HandleSet(req, res); });

        // DELETE /api/cache/{group}/{key}
        server_.Delete(R"(/api/cache/([^/]+)/([^/]+))",
                       [this](const httplib::Request& req, httplib::Response& res) { HandleDelete(req, res); });
    }

    void HandleGet(const httplib::Request& req, httplib::Response& res) {
        std::string group = req.matches[1];
        std::string key = req.matches[2];

        auto client = GetCacheClient(key);
        if (!client) {
            SendError(res, 503, "No cache service available");
            return;
        }

        pb::Request request;
        request.set_group(group);
        request.set_key(key);

        pb::GetResponse response;
        grpc::ClientContext context;

        auto status = client->Get(&context, request, &response);
        if (status.ok()) {
            nlohmann::json json_resp = {{"key", key}, {"value", response.value()}, {"group", group}};
            res.set_content(json_resp.dump(), "application/json");
        } else {
            SendError(res, 404, "Key not found");
        }
    }

    void HandleSet(const httplib::Request& req, httplib::Response& res) {
        std::string group = req.matches[1];
        std::string key = req.matches[2];

        auto client = GetCacheClient(key);
        if (!client) {
            SendError(res, 503, "No cache service available");
            return;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            body = {{"value", req.body}};
        }

        std::string value = body.value("value", "");
        if (value.empty()) {
            SendError(res, 400, "Value is required");
            return;
        }

        pb::Request request;
        request.set_group(group);
        request.set_key(key);
        request.set_value(value);

        pb::SetResponse response;
        grpc::ClientContext context;
        context.AddMetadata("is_gateway", "true");

        auto status = client->Set(&context, request, &response);
        if (status.ok() && response.value()) {
            nlohmann::json json_resp = {
                {"key", key}, {"value", value}, {"group", group}, {"success", response.value()}};
            res.set_content(json_resp.dump(), "application/json");
        } else {
            SendError(res, 500, "Failed to set value");
        }
    }

    void HandleDelete(const httplib::Request& req, httplib::Response& res) {
        std::string group = req.matches[1];
        std::string key = req.matches[2];

        auto client = GetCacheClient(key);
        if (!client) {
            SendError(res, 503, "No cache service available");
            return;
        }

        pb::Request request;
        request.set_group(group);
        request.set_key(key);

        pb::DeleteResponse response;
        grpc::ClientContext context;
        context.AddMetadata("is_gateway", "true");

        auto status = client->Delete(&context, request, &response);
        if (status.ok() && response.value()) {
            nlohmann::json json_resp = {{"key", key}, {"group", group}, {"deleted", response.value()}};
            res.set_content(json_resp.dump(), "application/json");
        } else {
            SendError(res, 500, "Failed to delete key");
        }
    }

    void StartServiceDiscovery() {
        discovery_thread_ = std::thread{[this] {
            std::string prefix = "/services/" + service_name_ + "/";
            while (true) {
                try {
                    auto resp = etcd_client_->ls(prefix).get();
                    if (resp.is_ok()) {
                        std::lock_guard lock{nodes_mutex_};
                        cache_nodes_.clear();
                        std::vector<std::string> nodes;
                        for (const auto& key : resp.keys()) {
                            spdlog::debug("Discovered key: {}", key);
                            std::string addr;
                            if (key.rfind(prefix, 0) == 0) {  // 检查是否以 prefix 开头
                                addr = key.substr(prefix.length());
                            }
                            if (addr.empty()) {
                                continue;
                            }
                            cache_nodes_.push_back(addr);
                            nodes.push_back(addr);
                        }
                        // 更新一致性哈希环
                        if (!nodes.empty()) {
                            consistent_hash_.Add(nodes);
                        }
                        spdlog::debug("Discovered {} cache nodes", cache_nodes_.size());
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Service discovery error: {}", e.what());
                }
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
        }};
    }

    auto GetCacheClient(const std::string& key) -> std::unique_ptr<pb::KCache::Stub> {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        if (cache_nodes_.empty()) {
            return nullptr;
        }

        // 使用一致性哈希选择节点
        std::string target_addr = consistent_hash_.Get(key);
        if (target_addr.empty()) {
            // 如果一致性哈希没有返回节点，回退到轮询
            target_addr = cache_nodes_[current_node_index_ % cache_nodes_.size()];
            current_node_index_++;
        }

        spdlog::debug("Routing key '{}' to node '{}'", key, target_addr);

        auto channel = grpc::CreateChannel(target_addr, grpc::InsecureChannelCredentials());
        return pb::KCache::NewStub(channel);
    }

    void SendError(httplib::Response& res, int code, const std::string& message) {
        nlohmann::json error = {{"error", message}, {"code", code}};
        res.status = code;
        res.set_content(error.dump(), "application/json");
    }

private:
    int port_;
    std::string etcd_addr_;
    std::string service_name_;
    httplib::Server server_;
    std::shared_ptr<etcd::Client> etcd_client_;

    std::vector<std::string> cache_nodes_;
    std::mutex nodes_mutex_;
    std::atomic<size_t> current_node_index_{0};
    std::thread discovery_thread_;
    kcache::ConsistentHashMap consistent_hash_;  // 一致性哈希环
};

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[http-gateway][%^%l%$] %v");

    try {
        HttpGateway gateway(FLAGS_http_port, FLAGS_etcd_endpoints, FLAGS_service_name);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        gateway.Start();
    } catch (const std::exception& e) {
        spdlog::error("Gateway failed: {}", e.what());
        return 1;
    }

    return 0;
}
