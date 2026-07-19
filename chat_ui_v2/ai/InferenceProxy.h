#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpConnection.h>

#include "sse/SseConnection.h"

namespace chatui_v2 {
namespace ai_proxy {

class InferenceProxy
{
public:
    struct BackendTarget
    {
        std::string host;
        uint16_t port { 8002 };
        std::string path { "/v1/chat/completions" };
        std::string authToken;
    };

    struct StreamRequest
    {
        BackendTarget target;
        std::string bodyJson;
        std::function<void(const std::string& token)> onToken;
        std::function<void(const std::string& error)> onError;
        std::function<void()> onDone;
    };

    InferenceProxy() = default;
    ~InferenceProxy() = default;

    InferenceProxy(const InferenceProxy&) = delete;
    InferenceProxy& operator=(const InferenceProxy&) = delete;

    std::string start(const sse::SseConnectionPtr& frontConn, StreamRequest req);
    void cancel(const std::string& streamId);

private:
    struct StreamContext;

    void onBackendConnection(const std::shared_ptr<StreamContext>& ctx,
                             const muduo::net::TcpConnectionPtr& backendConn);
    void onBackendMessage(const std::shared_ptr<StreamContext>& ctx,
                          muduo::net::Buffer* buf);
    void finish(const std::shared_ptr<StreamContext>& ctx,
                const std::string& error);

private:
    std::mutex mutex_;
    std::atomic<uint64_t> nextId_ { 1 };
    std::unordered_map<std::string, std::shared_ptr<StreamContext>> streams_;
};

} // namespace ai_proxy
} // namespace chatui_v2
