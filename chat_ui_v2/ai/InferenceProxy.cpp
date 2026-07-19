#include "ai/InferenceProxy.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>

#include "utils/JsonUtil.h"

namespace chatui_v2 {
namespace ai_proxy {

namespace {

std::string trimLeft(std::string s)
{
    auto it = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    s.erase(s.begin(), it);
    return s;
}

std::string buildRequest(const InferenceProxy::StreamRequest& req)
{
    std::ostringstream oss;
    oss << "POST " << req.target.path << " HTTP/1.1\r\n"
        << "Host: " << req.target.host << ":" << req.target.port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Accept: text/event-stream\r\n"
        << "Connection: keep-alive\r\n";
    if (!req.target.authToken.empty())
        oss << "Authorization: Bearer " << req.target.authToken << "\r\n";
    oss << "Content-Length: " << req.bodyJson.size() << "\r\n\r\n"
        << req.bodyJson;
    return oss.str();
}

std::string extractTokenFromSseData(const std::string& data)
{
    json j;
    try
    {
        j = json::parse(data);
    }
    catch (...)
    {
        return "";
    }

    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
    {
        const auto& c = j["choices"][0];
        if (c.contains("delta") && c["delta"].is_object())
        {
            const auto& d = c["delta"];
            if (d.contains("content") && d["content"].is_string())
                return d["content"].get<std::string>();
        }
    }
    if (j.contains("delta") && j["delta"].is_object())
    {
        const auto& d = j["delta"];
        if (d.contains("text") && d["text"].is_string())
            return d["text"].get<std::string>();
    }
    if (j.contains("content_block") && j["content_block"].is_object())
    {
        const auto& b = j["content_block"];
        if (b.contains("text") && b["text"].is_string())
            return b["text"].get<std::string>();
    }
    return "";
}

} // namespace

struct InferenceProxy::StreamContext
{
    std::string id;
    sse::SseConnectionPtr front;
    StreamRequest req;
    std::unique_ptr<muduo::net::TcpClient> client;
    muduo::net::TcpConnectionPtr backend;
    std::atomic<bool> finished { false };

    bool headersParsed { false };
    bool chunked { false };
    long statusCode { 0 };
    std::string recvBuf;
    std::string bodyBuf;
    std::string sseLineBuf;
    size_t chunkRemain { 0 };
};

std::string InferenceProxy::start(const sse::SseConnectionPtr& frontConn, StreamRequest req)
{
    if (!frontConn || !frontConn->connected() || !frontConn->tcpConnection())
        return "";

    auto ctx = std::make_shared<StreamContext>();
    ctx->id = "stream-" + std::to_string(nextId_.fetch_add(1));
    ctx->front = frontConn;
    ctx->req = std::move(req);

    auto* loop = frontConn->tcpConnection()->getLoop();
    ctx->client = std::make_unique<muduo::net::TcpClient>(
        loop,
        muduo::net::InetAddress(ctx->req.target.host, ctx->req.target.port),
        "InferProxy-" + ctx->id);
    std::weak_ptr<StreamContext> weakCtx = ctx;

    ctx->client->setConnectionCallback([this, weakCtx](const muduo::net::TcpConnectionPtr& conn) {
        auto locked = weakCtx.lock();
        if (!locked) return;
        onBackendConnection(locked, conn);
    });
    ctx->client->setMessageCallback([this, weakCtx](const muduo::net::TcpConnectionPtr&, muduo::net::Buffer* buf, muduo::Timestamp) {
        auto locked = weakCtx.lock();
        if (!locked) return;
        onBackendMessage(locked, buf);
    });
    ctx->client->enableRetry();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_[ctx->id] = ctx;
    }
    ctx->client->connect();
    return ctx->id;
}

void InferenceProxy::cancel(const std::string& streamId)
{
    std::shared_ptr<StreamContext> ctx;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) return;
        ctx = it->second;
    }
    finish(ctx, "stream canceled");
}

void InferenceProxy::onBackendConnection(const std::shared_ptr<StreamContext>& ctx,
                                         const muduo::net::TcpConnectionPtr& backendConn)
{
    if (!ctx || ctx->finished.load()) return;

    if (!backendConn->connected())
    {
        finish(ctx, "backend connection closed");
        return;
    }

    ctx->backend = backendConn;
    backendConn->send(buildRequest(ctx->req));
}

void InferenceProxy::onBackendMessage(const std::shared_ptr<StreamContext>& ctx,
                                      muduo::net::Buffer* buf)
{
    if (!ctx || ctx->finished.load() || !buf) return;

    ctx->recvBuf.append(buf->retrieveAllAsString());
    if (!ctx->headersParsed)
    {
        const size_t headerEnd = ctx->recvBuf.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return;

        const std::string headerBlock = ctx->recvBuf.substr(0, headerEnd);
        ctx->bodyBuf.append(ctx->recvBuf.substr(headerEnd + 4));
        ctx->recvBuf.clear();
        ctx->headersParsed = true;

        size_t lineEnd = headerBlock.find("\r\n");
        std::string statusLine = lineEnd == std::string::npos ? headerBlock : headerBlock.substr(0, lineEnd);
        {
            std::istringstream iss(statusLine);
            std::string httpVer;
            iss >> httpVer >> ctx->statusCode;
        }
        if (ctx->statusCode != 200)
        {
            finish(ctx, "backend HTTP " + std::to_string(ctx->statusCode));
            return;
        }

        std::string lowerHeader = headerBlock;
        std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        ctx->chunked = lowerHeader.find("transfer-encoding: chunked") != std::string::npos;
    }
    else
    {
        ctx->bodyBuf.append(ctx->recvBuf);
        ctx->recvBuf.clear();
    }

    auto processSsePayload = [this, &ctx](const std::string& bytes) {
        ctx->sseLineBuf.append(bytes);
        size_t pos = 0;
        while ((pos = ctx->sseLineBuf.find('\n')) != std::string::npos)
        {
            std::string line = ctx->sseLineBuf.substr(0, pos);
            ctx->sseLineBuf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) != 0) continue;

            std::string data = trimLeft(line.substr(5));
            if (data == "[DONE]")
            {
                finish(ctx, "");
                return;
            }

            std::string token = extractTokenFromSseData(data);
            if (!token.empty() && ctx->req.onToken)
                ctx->req.onToken(token);
        }
    };

    if (!ctx->chunked)
    {
        if (!ctx->bodyBuf.empty())
        {
            processSsePayload(ctx->bodyBuf);
            ctx->bodyBuf.clear();
        }
        return;
    }

    while (!ctx->bodyBuf.empty() && !ctx->finished.load())
    {
        if (ctx->chunkRemain == 0)
        {
            size_t rn = ctx->bodyBuf.find("\r\n");
            if (rn == std::string::npos) break;

            std::string lenHex = ctx->bodyBuf.substr(0, rn);
            size_t semi = lenHex.find(';');
            if (semi != std::string::npos) lenHex = lenHex.substr(0, semi);

            unsigned long n = 0;
            try { n = std::stoul(lenHex, nullptr, 16); }
            catch (...) { finish(ctx, "invalid chunk length"); return; }

            ctx->bodyBuf.erase(0, rn + 2);
            ctx->chunkRemain = static_cast<size_t>(n);
            if (ctx->chunkRemain == 0)
            {
                finish(ctx, "");
                return;
            }
        }

        if (ctx->bodyBuf.size() < ctx->chunkRemain + 2) break;
        std::string chunk = ctx->bodyBuf.substr(0, ctx->chunkRemain);
        ctx->bodyBuf.erase(0, ctx->chunkRemain + 2); // chunk + CRLF
        ctx->chunkRemain = 0;
        processSsePayload(chunk);
    }
}

void InferenceProxy::finish(const std::shared_ptr<StreamContext>& ctx,
                            const std::string& error)
{
    if (!ctx || ctx->finished.exchange(true)) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_.erase(ctx->id);
    }

    if (!error.empty() && ctx->req.onError)
        ctx->req.onError(error);
    if (ctx->req.onDone)
        ctx->req.onDone();

    if (ctx->backend && ctx->backend->connected())
        ctx->backend->shutdown();
    if (ctx->client)
        ctx->client->disconnect();
}

} // namespace ai_proxy
} // namespace chatui_v2
