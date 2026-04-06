#include "../../include/curl/CurlMultiManager.h"

#include "../../include/utils/JsonUtil.h"

#include <curl/curl.h>
#include <muduo/base/Logging.h>

#include <chrono>
#include <sstream>

namespace http
{
namespace curl
{

namespace
{

struct CurlGlobalGuard
{
    CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalGuard() { curl_global_cleanup(); }
};

CurlGlobalGuard g_curlGlobalGuard;

bool isRetryable(CURLcode code, long httpCode)
{
    if (code == CURLE_OPERATION_TIMEDOUT || code == CURLE_COULDNT_CONNECT || code == CURLE_RECV_ERROR)
        return true;
    if (httpCode == 429)
        return true;
    return httpCode >= 500 && httpCode <= 599;
}

} // namespace

struct CurlMultiManager::StreamContext
{
    StreamId id;
    ProviderHttpRequest req;
    StreamCallbacks callbacks;
    int retryCount { 0 };
    bool canceled { false };
    bool doneSignaled { false };
    std::string lineBuffer;
    StreamUsage usage;
    CURL* easy { nullptr };
    curl_slist* headerList { nullptr };
};

CurlMultiManager::CurlMultiManager()
{
    multi_ = curl_multi_init();
}

CurlMultiManager::~CurlMultiManager()
{
    stop();
    if (multi_)
    {
        curl_multi_cleanup(static_cast<CURLM*>(multi_));
        multi_ = nullptr;
    }
}

void CurlMultiManager::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
        return;
    loopThread_ = std::thread(&CurlMultiManager::runLoop, this);
}

void CurlMultiManager::stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false))
        return;
    cv_.notify_all();
    if (loopThread_.joinable())
        loopThread_.join();
}

StreamId CurlMultiManager::asyncStream(const ProviderHttpRequest& req, StreamCallbacks cb)
{
    std::shared_ptr<StreamContext> ctx = std::make_shared<StreamContext>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ctx->id = "stream_" + std::to_string(nextId_++);
        ctx->req = req;
        ctx->callbacks = std::move(cb);
        pending_.push_back(ctx);
        idMap_[ctx->id] = ctx;
    }
    cv_.notify_all();
    return ctx->id;
}

void CurlMultiManager::cancel(const StreamId& id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idMap_.find(id);
    if (it == idMap_.end())
        return;
    it->second->canceled = true;
}

void CurlMultiManager::runLoop()
{
    CURLM* multi = static_cast<CURLM*>(multi_);
    while (running_)
    {
        enqueueNewHandles();

        int runningHandles = 0;
        curl_multi_perform(multi, &runningHandles);
        processCompleted();

        int numfds = 0;
        curl_multi_poll(multi, nullptr, 0, 50, &numfds);

        if (runningHandles == 0)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                return !running_ || !pending_.empty();
            });
        }
    }

    std::vector<std::shared_ptr<StreamContext>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : handleMap_)
            all.push_back(kv.second);
        for (auto& kv : idMap_)
            all.push_back(kv.second);
        handleMap_.clear();
        idMap_.clear();
        pending_.clear();
    }
    for (auto& ctx : all)
    {
        if (ctx->easy)
        {
            curl_multi_remove_handle(multi, ctx->easy);
            curl_easy_cleanup(ctx->easy);
            ctx->easy = nullptr;
        }
        if (ctx->headerList)
        {
            curl_slist_free_all(ctx->headerList);
            ctx->headerList = nullptr;
        }
    }
}

void CurlMultiManager::enqueueNewHandles()
{
    std::vector<std::shared_ptr<StreamContext>> localPending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        localPending.swap(pending_);
    }
    if (localPending.empty())
        return;

    CURLM* multi = static_cast<CURLM*>(multi_);
    for (auto& ctx : localPending)
    {
        if (ctx->canceled)
            continue;

        ctx->easy = curl_easy_init();
        if (!ctx->easy)
        {
            if (ctx->callbacks.onError)
                ctx->callbacks.onError("curl_init_failed", "failed to init curl easy handle");
            continue;
        }

        for (const auto& h : ctx->req.headers)
            ctx->headerList = curl_slist_append(ctx->headerList, h.c_str());

        curl_easy_setopt(ctx->easy, CURLOPT_URL, ctx->req.url.c_str());
        curl_easy_setopt(ctx->easy, CURLOPT_HTTPHEADER, ctx->headerList);
        curl_easy_setopt(ctx->easy, CURLOPT_POST, 1L);
        curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDS, ctx->req.body.c_str());
        curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(ctx->req.body.size()));
        curl_easy_setopt(ctx->easy, CURLOPT_WRITEFUNCTION, &CurlMultiManager::writeCallback);
        curl_easy_setopt(ctx->easy, CURLOPT_WRITEDATA, ctx.get());
        curl_easy_setopt(ctx->easy, CURLOPT_PRIVATE, ctx.get());
        curl_easy_setopt(ctx->easy, CURLOPT_CONNECTTIMEOUT_MS, ctx->req.connectTimeoutMs);
        curl_easy_setopt(ctx->easy, CURLOPT_TIMEOUT_MS, ctx->req.totalTimeoutMs);
        curl_easy_setopt(ctx->easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(ctx->easy, CURLOPT_XFERINFOFUNCTION, &CurlMultiManager::progressCallback);
        curl_easy_setopt(ctx->easy, CURLOPT_XFERINFODATA, ctx.get());

        curl_multi_add_handle(multi, ctx->easy);
        std::lock_guard<std::mutex> lock(mutex_);
        handleMap_[ctx->easy] = ctx;
    }
}

void CurlMultiManager::processCompleted()
{
    CURLM* multi = static_cast<CURLM*>(multi_);
    int msgsInQueue = 0;
    while (CURLMsg* msg = curl_multi_info_read(multi, &msgsInQueue))
    {
        if (msg->msg != CURLMSG_DONE)
            continue;

        CURL* easy = msg->easy_handle;
        std::shared_ptr<StreamContext> ctx;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = handleMap_.find(easy);
            if (it != handleMap_.end())
                ctx = it->second;
        }
        if (!ctx)
        {
            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
            continue;
        }

        long httpCode = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpCode);
        CURLcode code = msg->data.result;

        curl_multi_remove_handle(multi, easy);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handleMap_.erase(easy);
        }
        curl_easy_cleanup(easy);
        ctx->easy = nullptr;
        if (ctx->headerList)
        {
            curl_slist_free_all(ctx->headerList);
            ctx->headerList = nullptr;
        }

        if (ctx->canceled)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idMap_.erase(ctx->id);
            continue;
        }

        if (code != CURLE_OK || httpCode >= 400)
        {
            if (tryRetry(ctx, code, httpCode))
                continue;

            if (ctx->callbacks.onError)
            {
                std::ostringstream oss;
                oss << "curl=" << curl_easy_strerror(code) << ", http=" << httpCode;
                ctx->callbacks.onError("upstream_failed", oss.str());
            }
            std::lock_guard<std::mutex> lock(mutex_);
            idMap_.erase(ctx->id);
            continue;
        }

        if (!ctx->doneSignaled)
        {
            if (ctx->callbacks.onUsage)
                ctx->callbacks.onUsage(ctx->usage);
            if (ctx->callbacks.onDone)
                ctx->callbacks.onDone();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        idMap_.erase(ctx->id);
    }
}

bool CurlMultiManager::tryRetry(const std::shared_ptr<StreamContext>& ctx, CURLcode code, long httpCode)
{
    if (!isRetryable(code, httpCode))
        return false;
    if (ctx->retryCount >= ctx->req.maxRetries)
        return false;
    ++ctx->retryCount;

    ctx->lineBuffer.clear();
    ctx->usage = {};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(ctx);
    }
    cv_.notify_all();
    return true;
}

size_t CurlMultiManager::writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* rawCtx = static_cast<StreamContext*>(userdata);
    if (!rawCtx || rawCtx->canceled)
        return 0;
    const size_t len = size * nmemb;
    auto fakeShared = std::shared_ptr<StreamContext>(rawCtx, [](StreamContext*) {});
    parseSseChunk(fakeShared, std::string(ptr, len));
    return len;
}

int CurlMultiManager::progressCallback(void* clientp, long, long, long, long)
{
    auto* ctx = static_cast<StreamContext*>(clientp);
    if (ctx && ctx->canceled)
        return 1;
    return 0;
}

void CurlMultiManager::parseSseChunk(const std::shared_ptr<StreamContext>& ctx, const std::string& chunk)
{
    ctx->lineBuffer.append(chunk);
    size_t pos = 0;
    while ((pos = ctx->lineBuffer.find('\n')) != std::string::npos)
    {
        std::string line = ctx->lineBuffer.substr(0, pos);
        ctx->lineBuffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("data: ", 0) != 0)
            continue;
        std::string data = line.substr(6);
        if (data == "[DONE]")
        {
            ctx->doneSignaled = true;
            if (ctx->callbacks.onUsage)
                ctx->callbacks.onUsage(ctx->usage);
            if (ctx->callbacks.onDone)
                ctx->callbacks.onDone();
            continue;
        }
        emitTokenFromJson(ctx, data);
    }
}

void CurlMultiManager::emitTokenFromJson(const std::shared_ptr<StreamContext>& ctx, const std::string& data)
{
    json j;
    try
    {
        j = json::parse(data);
    }
    catch (...)
    {
        return;
    }

    if (j.contains("usage"))
    {
        auto usage = j["usage"];
        if (usage.contains("input_tokens")) ctx->usage.inputTokens = usage["input_tokens"].get<int>();
        if (usage.contains("output_tokens")) ctx->usage.outputTokens = usage["output_tokens"].get<int>();
        if (usage.contains("prompt_tokens")) ctx->usage.inputTokens = usage["prompt_tokens"].get<int>();
        if (usage.contains("completion_tokens")) ctx->usage.outputTokens = usage["completion_tokens"].get<int>();
    }

    std::string token;
    if (j.contains("delta") && j["delta"].is_object() && j["delta"].contains("text"))
        token = j["delta"]["text"].get<std::string>();
    else if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
    {
        auto c = j["choices"][0];
        if (c.contains("delta") && c["delta"].is_object() && c["delta"].contains("content") && c["delta"]["content"].is_string())
            token = c["delta"]["content"].get<std::string>();
    }
    else if (j.contains("content_block") && j["content_block"].is_object() && j["content_block"].contains("text"))
        token = j["content_block"]["text"].get<std::string>();

    if (!token.empty() && ctx->callbacks.onToken)
        ctx->callbacks.onToken(token);
}

} // namespace curl
} // namespace http
