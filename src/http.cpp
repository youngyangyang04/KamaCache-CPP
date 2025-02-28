#include "http.h"

#include <cstddef>
#include <format>
#include <string>

#include "cache.h"

namespace kcache {

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

    std::string value = ret.value()->ToString();
    res.set_content(value, "application/octet-stream");
}

}  // namespace kcache