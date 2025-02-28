#ifndef HTTP_H_
#define HTTP_H_

#include <httplib.h>

#include <string>

namespace kcache {

constexpr std::string default_base_path = "/kcache/";

class HTTPPool {
public:
    HTTPPool(const std::string& host = "0.0.0.0", int port = 8080, const std::string& base_path = default_base_path)
        : host_(host), port_(port), base_path_(base_path) {
        server_.Get(".*", [&](const httplib::Request& req, httplib::Response& res) { HandleRequest(req, res); });
    }

    void Start() { server_.listen(host_, port_); }

    auto GetHost() -> std::string { return host_; }
    auto GetPort() -> int { return port_; }

private:
    void HandleRequest(const httplib::Request& request, httplib::Response& response);

private:
    std::string host_;
    int port_;
    std::string base_path_;
    httplib::Server server_;
};

}  // namespace kcache

#endif /* HTTP_H_ */
