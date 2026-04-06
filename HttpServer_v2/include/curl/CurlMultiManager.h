#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <curl/curl.h>

namespace http
{
namespace curl
{

using StreamId = std::string;

struct ProviderHttpRequest
{
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long connectTimeoutMs { 5000 };
    long totalTimeoutMs { 120000 };
    int maxRetries { 2 };
};

struct StreamUsage
{
    int inputTokens { 0 };
    int outputTokens { 0 };
};

struct StreamCallbacks
{
    std::function<void(const std::string&)> onToken;
    std::function<void(const StreamUsage&)> onUsage;
    std::function<void()> onDone;
    std::function<void(const std::string& code, const std::string& message)> onError;
};

class CurlMultiManager
{
public:
    CurlMultiManager();
    ~CurlMultiManager();

    CurlMultiManager(const CurlMultiManager&) = delete;
    CurlMultiManager& operator=(const CurlMultiManager&) = delete;

    void start();
    void stop();

    StreamId asyncStream(const ProviderHttpRequest& req, StreamCallbacks cb);
    void cancel(const StreamId& id);

private:
    struct StreamContext;

    void runLoop();
    void enqueueNewHandles();
    void processCompleted();
    bool tryRetry(const std::shared_ptr<StreamContext>& ctx, CURLcode code, long httpCode);

    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static int progressCallback(void* clientp, long, long, long, long);
    static void parseSseChunk(const std::shared_ptr<StreamContext>& ctx, const std::string& chunk);
    static void emitTokenFromJson(const std::shared_ptr<StreamContext>& ctx, const std::string& data);

private:
    std::atomic<bool> running_ { false };
    std::thread loopThread_;

    void* multi_ { nullptr };

    std::mutex mutex_;
    std::condition_variable cv_;
    uint64_t nextId_ { 1 };

    std::vector<std::shared_ptr<StreamContext>> pending_;
    std::unordered_map<void*, std::shared_ptr<StreamContext>> handleMap_;
    std::unordered_map<StreamId, std::shared_ptr<StreamContext>> idMap_;
};

} // namespace curl
} // namespace http
