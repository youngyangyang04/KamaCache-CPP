#include "http.h"

#include <cstddef>
#include <format>
#include <memory>
#include <mutex>
#include <string>

#include "cache.h"
#include "consistent_hash.h"
#include "kcache.pb.h"

namespace kcache {

auto Peer::Get(pb::Request* in, pb::Response* out) -> bool {
    // 还记得请求的格式嘛：http://<host>:<port>/<basepath>/<groupname>/<key>
    // base_url_ 对应了 http://<host>:<port>

    httplib::Client cli(base_url_);
    auto res = cli.Get(std::format("{}{}/{}", base_path_, in->group(), in->key()));
    if (!res) {
        return false;
    }

    if (res->status != 200) {
        std::cout << std::format("server returned: {}\n", res->status);
        return false;
    }

    // 从 body 中反序列化解析出 protobuf 数据
    if (!out->ParseFromString(res->body)) {
        std::cout << "protobuf decoding err!\n";
        return false;
    }

    return true;
}

void HTTPPool::SetPeers(const std::vector<std::string>& peer_addrs) {
    std::lock_guard lock{mtx_};
    peers_hash_ = ConsistentHash{default_relicas};
    peers_hash_.AddNodes(peer_addrs);
    peers_.clear();
    for (const auto& peer_addr : peer_addrs) {
        peers_[peer_addr] = Peer{peer_addr, base_path_};
    }
    // peers_hash_.PrintHashRing();
}

auto HTTPPool::GetPeer(const std::string& key) -> Peer* {
    std::lock_guard lock{mtx_};
    if (auto peer_name = peers_hash_.GetNodeName(key); !peer_name.empty() && peer_name != self_) {
        std::cout << std::format("pick peer: {}\n", peer_name);
        return &peers_[peer_name];
    }
    return nullptr;
}

void HTTPPool::HandleRequest(const httplib::Request& req, httplib::Response& res) {
    // 请求格式为：http://<host>:<port>/<basepath>/<groupname>/<key>
    // 如果请求前缀不为 base_path_ 则说明不是使用缓存服务
    if (req.path.substr(0, base_path_.size()) != base_path_) {
        res.status = 400;
        res.set_content("Bad Request", "text/plain");
        return;
    }

    // remaining 相当于从 <groupname> 开始
    std::string remaining = req.path.substr(base_path_.size());
    size_t pos = remaining.find('/');
    if (pos == std::string::npos || pos == 0 || pos == remaining.size() - 1) {
        res.status = 400;
        res.set_content("Bad Request", "text/plain");
        return;
    }

    // 分割 <groupname> 和 <key>
    std::string group_name = remaining.substr(0, pos);
    std::string key = remaining.substr(pos + 1);

    std::cout << std::format("Request for group: {}, key: {}\n", group_name, key);

    auto cache_group = GetCacheGroup(group_name);
    if (cache_group == nullptr) {
        res.status = 404;
        res.set_content("No such group: " + group_name, "text/plain");
        return;
    }

    auto ret = cache_group->Get(key);
    if (!ret) {
        res.status = 500;
        res.set_content(key + "is not exit", "text/plain");
        return;
    }

    // 使用 protobuf 将结果序列化后再响应回去

    std::string body;
    pb::Response pb_resp{};
    pb_resp.set_value(ret.value()->ToString());

    if (!pb_resp.SerializeToString(&body)) {
        res.status = 500;
        res.set_content("protobuf failed to serialize response", "text/plain");
        return;
    }

    // 如果对序列化的结果感兴趣，可以使用下面的代码打印
    // auto ToHex = [](const std::string& binaryData) {
    //     std::ostringstream oss;
    //     for (unsigned char c : binaryData) {
    //         oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    //     }
    //     return oss.str();
    // };
    // std::cout << std::format("[protobuf] {}\n", ToHex(body));

    res.set_header("Content-Type", "application/octet-stream");
    res.set_content(body, "application/octet-stream");
}

}  // namespace kcache