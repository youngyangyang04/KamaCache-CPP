// 通过线程安全的缓存机制 ，确保对同一个键（key）的多次并发请求只会执行一次实际操作，其他请求会等待并复用结果

#ifndef SINGLEFLIGHT_H_
#define SINGLEFLIGHT_H_

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "kcache/cache.h"

namespace kcache {

class SingleFlight {
    using Result = std::optional<ByteView>;
    using Func = std::function<Result()>;

public:
    Result Do(const std::string& key, Func func) {
        std::unique_lock<std::mutex> glock(mtx_);

        // 检查是否有进行中的调用
        if (map_.find(key) != map_.end()) {
            auto existing_call = map_[key];
            glock.unlock();  // 释放组锁，避免阻塞其他键的处理

            // 直接等待 future 的结果
            auto result = existing_call->fut.get();
            return result;
        }

        // 创建新的调用对象
        auto new_call = std::make_shared<Call>();
        map_[key] = new_call;
        glock.unlock();

        // 执行用户函数并设置 promise
        Result val = func();
        new_call->prom.set_value(val);

        // 确保从映射中删除条目
        std::lock_guard<std::mutex> lock(mtx_);
        map_.erase(key);

        return val;
    }

private:
    struct Call {
        std::promise<Result> prom;
        std::shared_future<Result> fut = prom.get_future().share();
    };

    std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<Call>> map_;
};

}  // namespace kcache

#endif /* SINGLEFLIGHT_H_ */
