#include <string>
#include <thread>

#include <gflags/gflags.h>
#include <httplib.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "kcache/client.h"

DEFINE_int32(http_port, 9000, "HTTP服务端口");
DEFINE_string(etcd_endpoints, "http://127.0.0.1:2379", "etcd地址");
DEFINE_string(service_name, "kcache", "缓存服务名称");

using namespace kcache;

class HttpGateway {
public:
    HttpGateway(int port, const std::string& etcd_addr, const std::string& svc_name) : port_(port) {
        kcache_client_ = std::make_unique<KCacheClient>(etcd_addr, svc_name);
        server_.set_payload_max_length(4 << 20);
        SetupRoutes();
    }

    ~HttpGateway() { server_.stop(); }

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

        auto value = kcache_client_->Get(group, key);
        if (value) {
            nlohmann::json json_resp = {{"key", key}, {"value", *value}, {"group", group}};
            res.set_content(json_resp.dump() + "\n", "application/json");
        } else {
            SendError(res, 404, "Key not found or service unavailable");
        }
    }

    void HandleSet(const httplib::Request& req, httplib::Response& res) {
        std::string group = req.matches[1];
        std::string key = req.matches[2];

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

        bool success = kcache_client_->Set(group, key, value);
        if (success) {
            nlohmann::json json_resp = {{"key", key}, {"value", value}, {"group", group}, {"success", true}};
            res.set_content(json_resp.dump() + "\n", "application/json");
        } else {
            SendError(res, 500, "Failed to Set value");
        }
    }

    void HandleDelete(const httplib::Request& req, httplib::Response& res) {
        std::string group = req.matches[1];
        std::string key = req.matches[2];

        bool success = kcache_client_->Delete(group, key);
        if (success) {
            nlohmann::json json_resp = {{"key", key}, {"group", group}, {"deleted", true}};
            res.set_content(json_resp.dump() + "\n", "application/json");
        } else {
            SendError(res, 500, "Failed to delete key");
        }
    }

    void SendError(httplib::Response& res, int code, const std::string& message) {
        nlohmann::json error = {{"error", message}, {"code", code}};
        res.status = code;
        res.set_content(error.dump(), "application/json");
    }

private:
    int port_;
    httplib::Server server_;
    std::unique_ptr<KCacheClient> kcache_client_;
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
