#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/AIConfig.h"
#include "ai/AIFactory.h"
#include "ai/InferenceProxy.h"
#include "ai/ModelRegister.h"
#include "api/ConversationHandlers.h"
#include "api/MessageHandler.h"
#include "auth/AuthHandlers.h"
#include "common/JsonLite.h"
#include "dao/ConversationDao.h"
#include "dao/MessageDao.h"
#include "dao/UserDao.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "http/HttpServer.h"
#include "middleware/rate_limit/RateLimitMiddleware.h"
#include "queue/RedisMessageQueue.h"
#include "session/RedisSessionStorage.h"
#include "session/SessionManager.h"
#include "session/SessionStorage.h"
#include "utils/db/DbConnectionPool.h"
#include "utils/JsonUtil.h"
#include "utils/MysqlUtil.h"

namespace {

std::string g_page;

std::string readFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool loadPage()
{
    g_page = readFile("./chatui.html");
    if (!g_page.empty()) return true;
    g_page = readFile("../chat_ui_v2/chatui.html");
    return !g_page.empty();
}

bool hasColumn(const std::string& dbName,
               const std::string& tableName,
               const std::string& columnName)
{
    auto conn = http::db::DbConnectionPool::getInstance().getConnection();
    std::unique_ptr<sql::ResultSet> rs(
        conn->executeQuery(
            "SELECT COUNT(*) AS cnt FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND COLUMN_NAME = ?",
            dbName, tableName, columnName));
    if (rs && rs->next()) return rs->getInt("cnt") > 0;
    return false;
}

void ensureSchemaV2(const std::string& dbName)
{
    auto conn = http::db::DbConnectionPool::getInstance().getConnection();

    conn->executeUpdate(
        "CREATE TABLE IF NOT EXISTS users ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "username VARCHAR(64) NOT NULL,"
        "password_hash VARCHAR(128) NOT NULL,"
        "salt VARCHAR(64) NOT NULL,"
        "created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

    conn->executeUpdate(
        "CREATE TABLE IF NOT EXISTS conversations ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "user_id BIGINT UNSIGNED NOT NULL,"
        "title VARCHAR(256) NOT NULL DEFAULT 'New Chat',"
        "created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "INDEX idx_conv_user (user_id),"
        "CONSTRAINT fk_conv_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

    conn->executeUpdate(
        "CREATE TABLE IF NOT EXISTS messages ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "conversation_id BIGINT UNSIGNED NOT NULL,"
        "role VARCHAR(16) NOT NULL,"
        "content TEXT NOT NULL,"
        "created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "INDEX idx_msg_conv (conversation_id),"
        "CONSTRAINT fk_msg_conv FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

    if (!hasColumn(dbName, "users", "email"))
        conn->executeUpdate("ALTER TABLE users ADD COLUMN email VARCHAR(128) NOT NULL DEFAULT '' AFTER username");
    if (!hasColumn(dbName, "users", "updated_at"))
        conn->executeUpdate("ALTER TABLE users ADD COLUMN updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP AFTER created_at");

    // 回填旧用户的 email，避免新唯一约束失败
    conn->executeUpdate("UPDATE users SET email = CONCAT(username, '@local.invalid') WHERE email = '' OR email IS NULL");

    if (!hasColumn(dbName, "conversations", "model"))
        conn->executeUpdate("ALTER TABLE conversations ADD COLUMN model VARCHAR(128) NOT NULL DEFAULT '' AFTER title");
    if (!hasColumn(dbName, "conversations", "system_prompt"))
        conn->executeUpdate("ALTER TABLE conversations ADD COLUMN system_prompt TEXT NULL AFTER model");
    if (!hasColumn(dbName, "messages", "event_id"))
        conn->executeUpdate("ALTER TABLE messages ADD COLUMN event_id VARCHAR(128) NULL AFTER content");

    // 索引/约束幂等处理：已存在时报错则忽略
    try { conn->executeUpdate("ALTER TABLE users ADD UNIQUE KEY uk_users_email (email)"); } catch (...) {}
    try { conn->executeUpdate("ALTER TABLE users ADD UNIQUE KEY uk_users_username (username)"); } catch (...) {}
    try { conn->executeUpdate("ALTER TABLE messages ADD UNIQUE KEY uk_messages_event_id (event_id)"); } catch (...) {}
}

std::string env(const char* key, const std::string& def = "")
{
    const char* v = std::getenv(key);
    return (v && v[0]) ? std::string(v) : def;
}

int envInt(const char* key, int def)
{
    const char* v = std::getenv(key);
    if (!v || !v[0]) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

std::string buildRedisUri()
{
    const std::string host = env("REDIS_HOST", "127.0.0.1");
    const std::string port = env("REDIS_PORT", "6379");
    const std::string pass = env("REDIS_PASS", "");
    const std::string db = env("REDIS_DB", "0");

    std::string uri = "tcp://";
    if (!pass.empty()) uri += ":" + pass + "@";
    uri += host + ":" + port;
    uri += "/" + db;
    return uri;
}

std::string genEventId()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned long long> dist;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    std::ostringstream oss;
    oss << now << "-" << std::hex << dist(rng);
    return oss.str();
}

std::string buildInferRequestJson(const std::vector<ai::Message>& messages,
                                  const std::string& model,
                                  int maxTokens)
{
    json payload;
    payload["stream"] = true;
    if (!model.empty()) payload["model"] = model;
    if (maxTokens > 0) payload["max_tokens"] = maxTokens;
    payload["messages"] = json::array();
    for (const auto& msg : messages)
    {
        payload["messages"].push_back({
            {"role", msg.role},
            {"content", msg.content},
        });
    }
    return payload.dump();
}

std::string buildUserJson(const chatui_v2::dao::User& u)
{
    return std::string("{\"id\":") + std::to_string(u.id)
        + ",\"username\":\"" + chatui_v2::json::escape(u.username)
        + "\",\"name\":\"" + chatui_v2::json::escape(u.username)
        + "\",\"email\":\"" + chatui_v2::json::escape(u.email) + "\"}";
}

} // namespace

int main(int argc, char** argv)
{
    int port = 8088;
    if (argc > 1) port = std::atoi(argv[1]);

    if (!loadPage())
    {
        g_page = "<!doctype html><html><body><h2>chatui.html not found</h2></body></html>";
    }

    const std::string dbHost = env("DB_HOST", "127.0.0.1");
    const std::string dbUser = env("DB_USER", "root");
    const std::string dbPass = env("DB_PASS", "123456");
    const std::string dbName = env("DB_NAME", "chat_app");
    const int dbPoolSize = std::atoi(env("DB_POOL_SIZE", "10").c_str());
    http::MysqlUtil::init(dbHost, dbUser, dbPass, dbName, dbPoolSize);
    ensureSchemaV2(dbName);

    ai::AIConfig aiConfig;
    bool loaded = aiConfig.load("./config.json")
               || aiConfig.load("../chat_ui_v2/config.json")
               || aiConfig.load("../chat_ui/config.json");

    if (!loaded)
    {
        ai::ModelConfig c;
        c.baseUrl = env("ANTHROPIC_BASE_URL", "https://api.anthropic.com");
        c.apiKey = env("ANTHROPIC_AUTH_TOKEN", "");
        c.model = env("ANTHROPIC_MODEL", "claude-sonnet-4-5-20250929");
        c.maxTokens = 4096;
        c.timeout = 120;

        ai::AIFactory::instance().registerModel(
            "claude",
            [c](const ai::ModelConfig&) {
                return std::make_unique<ai::ClaudeStrategy>(c);
            });
        aiConfig.setFallbackModel("claude");
    }

    http::HttpServer server(port, "chatui-v2");
    server.setThreadNum(4);
    server.setWorkerThreads(4);

    const std::string redisUri = buildRedisUri();
    const int sessionTtlSec = envInt("SESSION_TTL_SEC", 3600);
    std::unique_ptr<http::session::SessionStorage> storage;
    try
    {
        storage = std::make_unique<http::session::RedisSessionStorage>(redisUri, sessionTtlSec);
    }
    catch (const std::exception&)
    {
        storage = std::make_unique<http::session::MemorySessionStorage>();
    }
    auto sm = std::make_unique<http::session::SessionManager>(std::move(storage));
    auto* smPtr = sm.get();
    server.setSessionManager(std::move(sm));

    http::middleware::RateLimitConfig rlCfg;
    rlCfg.loginPerMinute = envInt("LOGIN_RL_PER_MIN", 5);
    rlCfg.loginPer15Min = envInt("LOGIN_RL_PER_15MIN", 20);
    rlCfg.ssePerMinute = envInt("SSE_RL_PER_MIN", 30);
    rlCfg.sseBurst = envInt("SSE_RL_BURST", 10);
    rlCfg.sseConnLimit = envInt("SSE_CONN_LIMIT", 3);
    rlCfg.sseConnTtlSec = envInt("SSE_CONN_TTL_SEC", 3600);
    std::shared_ptr<http::middleware::RateLimiter> limiter;
    std::shared_ptr<chatui_v2::queue::RedisMessagePublisher> msgPublisher;
    std::unique_ptr<chatui_v2::queue::RedisMessageConsumer> msgConsumer;

    const std::string streamKey = env("STREAM_KEY", "chat:msg:write");
    const std::string streamGroup = env("STREAM_GROUP", "msg-writers");
    const std::string streamConsumer = env("STREAM_CONSUMER", "writer-1");
    const int streamBlockMs = envInt("STREAM_BLOCK_MS", 2000);
    try
    {
        limiter = std::make_shared<http::middleware::RateLimiter>(redisUri, rlCfg);
        server.addMiddleware(std::make_shared<http::middleware::RateLimitMiddleware>(limiter));
        msgPublisher = std::make_shared<chatui_v2::queue::RedisMessagePublisher>(redisUri, streamKey);
        msgConsumer = std::make_unique<chatui_v2::queue::RedisMessageConsumer>(
            redisUri, streamKey, streamGroup, streamConsumer, streamBlockMs);
        msgConsumer->start();
    }
    catch (const std::exception&)
    {
        limiter.reset();
        msgPublisher.reset();
        msgConsumer.reset();
    }

    chatui_v2::ai_proxy::InferenceProxy::BackendTarget inferTarget;
    inferTarget.host = env("INFER_HOST", "127.0.0.1");
    inferTarget.port = static_cast<uint16_t>(envInt("INFER_PORT", 8002));
    inferTarget.path = env("INFER_PATH", "/v1/chat/completions");
    inferTarget.authToken = env("INFER_AUTH_TOKEN", "");
    chatui_v2::ai_proxy::InferenceProxy inferenceProxy;

    server.Get("/", [](const http::HttpRequest&, http::HttpResponse* resp) {
        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setContentType("text/html; charset=utf-8");
        resp->setBody(g_page);
    });

    server.Get("/api/health", [&aiConfig](const http::HttpRequest&, http::HttpResponse* resp) {
        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setContentType("application/json; charset=utf-8");

        auto models = ai::AIFactory::instance().listModels();
        std::string arr = "[";
        for (size_t i = 0; i < models.size(); ++i)
        {
            if (i > 0) arr += ',';
            arr += "\"" + chatui_v2::json::escape(models[i]) + "\"";
        }
        arr += "]";

        resp->setBody(std::string("{\"status\":\"ok\",\"default_model\":\"")
                      + chatui_v2::json::escape(aiConfig.defaultModel())
                      + "\",\"models\":" + arr + "}");
    });

    server.Post("/api/auth/register", std::make_shared<chatui_v2::auth::RegisterHandler>(smPtr));
    server.Post("/api/auth/login", std::make_shared<chatui_v2::auth::LoginHandler>(smPtr));
    server.Post("/api/auth/logout", std::make_shared<chatui_v2::auth::LogoutHandler>(smPtr));

    auto meHandler = [smPtr](const http::HttpRequest& req, http::HttpResponse* resp) {
        resp->setContentType("application/json; charset=utf-8");
        auto session = smPtr->getSession(req, resp);
        std::string uidStr = session->getValue("user_id");
        if (uidStr.empty())
        {
            resp->setStatusCode(http::HttpResponse::k401Unauthorized);
            resp->setBody(R"({"error":"not logged in"})");
            return;
        }

        int64_t uid = 0;
        try { uid = std::stoll(uidStr); } catch (...) { uid = 0; }
        auto user = chatui_v2::dao::UserDao::findById(uid);
        if (user.id == 0)
        {
            resp->setStatusCode(http::HttpResponse::k401Unauthorized);
            resp->setBody(R"({"error":"user not found"})");
            return;
        }

        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setBody(std::string("{\"ok\":true,\"user\":") + buildUserJson(user) + "}");
    };
    server.Get("/api/users/me", meHandler);
    server.Get("/api/auth/me", meHandler);

    auto convList = std::make_shared<chatui_v2::api::ConversationListHandler>(smPtr);
    auto convDetail = std::make_shared<chatui_v2::api::ConversationDetailHandler>(smPtr);
    auto msgHandler = std::make_shared<chatui_v2::api::MessageHandler>(smPtr);

    server.Get("/api/conversations", convList);
    server.Post("/api/conversations", convList);
    server.addRoute(http::HttpRequest::kPatch, "/api/conversations/:id", convDetail);
    server.addRoute(http::HttpRequest::kPut, "/api/conversations/:id", convDetail);
    server.addRoute(http::HttpRequest::kDelete, "/api/conversations/:id", convDetail);
    server.addRoute(http::HttpRequest::kGet, "/api/conversations/:id/messages", msgHandler);

    server.Sse("/api/chat/sse", [smPtr, limiter, msgPublisher, inferTarget, &inferenceProxy](const http::HttpRequest& req, const sse::SseConnectionPtr& sse) {
        if (!sse || !sse->connected()) return;

        http::HttpResponse fakeResp(false);
        auto session = smPtr->getSession(req, &fakeResp);
        std::string uidStr = session ? session->getValue("user_id") : "";
        int64_t userId = 0;
        try { if (!uidStr.empty()) userId = std::stoll(uidStr); } catch (...) { userId = 0; }

        if (userId <= 0)
        {
            sse->send(R"({"type":"error","message":"not logged in"})", "error");
            sse->send(R"({"type":"done"})", "done");
            sse->close();
            return;
        }

        const std::string sseUserKey = "u:" + std::to_string(userId);
        int connRetry = 0;
        if (limiter && !limiter->acquireSseConnectionSlot(sseUserKey, &connRetry))
        {
            sse->send(std::string("{\"type\":\"error\",\"message\":\"too many concurrent sse connections\",\"retry_after\":")
                      + std::to_string(std::max(1, connRetry)) + "}", "error");
            sse->send(R"({"type":"done"})", "done");
            sse->close();
            return;
        }
        auto releaseConnSlot = [&]() {
            if (limiter && !sseUserKey.empty()) limiter->releaseSseConnectionSlot(sseUserKey);
        };

        const std::string body = req.getBody();
        int64_t convId = chatui_v2::json::extractInt64(body, "conversation_id", 0);
        const std::string requestedModel = chatui_v2::json::extractString(body, "model");
        const std::string systemPrompt = chatui_v2::json::extractString(body, "system_prompt");
        int maxTokens = static_cast<int>(chatui_v2::json::extractInt64(body, "max_tokens", 2048));
        if (maxTokens <= 0) maxTokens = 2048;
        auto msgPairs = chatui_v2::json::extractMessages(body);

        std::vector<ai::Message> messages;
        messages.reserve(msgPairs.size() + 1);
        if (!systemPrompt.empty()) messages.push_back({"system", systemPrompt});
        for (const auto& p : msgPairs)
        {
            if (!p.first.empty()) messages.push_back({p.first, p.second});
        }

        if (messages.empty())
        {
            sse->send(R"({"type":"error","message":"messages required"})", "error");
            sse->send(R"({"type":"done"})", "done");
            sse->close();
            releaseConnSlot();
            return;
        }

        if (convId > 0)
        {
            auto conv = chatui_v2::dao::ConversationDao::findById(convId, userId);
            if (conv.id == 0) convId = 0;
        }

        std::string lastUserMsg;
        for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        {
            if (it->role == "user")
            {
                lastUserMsg = it->content;
                break;
            }
        }

        std::string title = "新对话";
        if (!lastUserMsg.empty())
        {
            title = lastUserMsg.substr(0, 28);
            if (lastUserMsg.size() > 28) title += "...";
        }

        if (convId == 0)
        {
            convId = chatui_v2::dao::ConversationDao::create(userId, title, requestedModel, systemPrompt);
            auto conv = chatui_v2::dao::ConversationDao::findById(convId, userId);
            sse->send(std::string("{\"type\":\"meta\",\"conversation_id\":") + std::to_string(conv.id)
                      + ",\"model\":\"" + chatui_v2::json::escape(conv.model) + "\"}", "meta");
        }

        if (!lastUserMsg.empty() && convId > 0)
        {
            try
            {
                if (msgPublisher)
                {
                    msgPublisher->publish(chatui_v2::queue::MessageWriteEvent{
                        genEventId(), convId, "user", lastUserMsg
                    });
                }
                else
                {
                    chatui_v2::dao::MessageDao::insert(convId, "user", lastUserMsg);
                }
            }
            catch (const std::exception&)
            {
                chatui_v2::dao::MessageDao::insert(convId, "user", lastUserMsg);
            }
        }

        sse->send(std::string("{\"type\":\"meta\",\"provider\":\"inference-proxy\",\"model\":\"")
                  + chatui_v2::json::escape(requestedModel) + "\"}", "meta");

        struct StreamState {
            std::shared_ptr<http::middleware::RateLimiter> limiter;
            std::string limiterKey;
            std::atomic<bool> slotReleased { false };
            std::atomic<bool> doneSent { false };
            int64_t conversationId { 0 };
            std::string full;
            sse::SseConnectionPtr sseConn;
            std::shared_ptr<chatui_v2::queue::RedisMessagePublisher> publisher;
        };
        auto state = std::make_shared<StreamState>();
        state->limiter = limiter;
        state->limiterKey = sseUserKey;
        state->conversationId = convId;
        state->sseConn = sse;
        state->publisher = msgPublisher;

        auto finalize = [state]() {
            if (!state->doneSent.exchange(true))
            {
                if (state->conversationId > 0 && !state->full.empty())
                {
                    try
                    {
                        if (state->publisher)
                        {
                            state->publisher->publish(chatui_v2::queue::MessageWriteEvent{
                                genEventId(), state->conversationId, "assistant", state->full
                            });
                        }
                        else
                        {
                            chatui_v2::dao::MessageDao::insert(state->conversationId, "assistant", state->full);
                        }
                    }
                    catch (const std::exception&)
                    {
                        chatui_v2::dao::MessageDao::insert(state->conversationId, "assistant", state->full);
                    }
                    chatui_v2::dao::ConversationDao::touch(state->conversationId);
                }

                if (state->sseConn && state->sseConn->connected())
                    state->sseConn->send(R"({"type":"done"})", "done");
                if (state->sseConn)
                    state->sseConn->close();
            }
            if (state->limiter && !state->limiterKey.empty() && !state->slotReleased.exchange(true))
            {
                state->limiter->releaseSseConnectionSlot(state->limiterKey);
            }
        };

        chatui_v2::ai_proxy::InferenceProxy::StreamRequest streamReq;
        streamReq.target = inferTarget;
        streamReq.bodyJson = buildInferRequestJson(messages, requestedModel, maxTokens);
        streamReq.onToken = [state](const std::string& token) {
            if (!state->sseConn || !state->sseConn->connected())
            {
                return;
            }
            state->full += token;
            state->sseConn->send(std::string("{\"type\":\"token\",\"content\":\"") + chatui_v2::json::escape(token) + "\"}");
        };
        streamReq.onError = [state](const std::string& err) {
            if (!state->sseConn || !state->sseConn->connected()) return;
            state->sseConn->send(std::string("{\"type\":\"error\",\"message\":\"") + chatui_v2::json::escape(err) + "\"}", "error");
        };
        streamReq.onDone = [finalize]() {
            finalize();
        };

        const std::string streamId = inferenceProxy.start(sse, std::move(streamReq));
        if (streamId.empty())
        {
            if (sse->connected())
                sse->send(R"({"type":"error","message":"failed to start backend stream"})", "error");
            finalize();
        }
    });

    server.Ws("/api/chat/ws",
        [](const ws::WsConnectionPtr& ws) {
            if (!ws) return;
            ws->sendText(R"({"type":"meta","message":"ws ready"})");
        },
        [](const ws::WsConnectionPtr& ws, const std::string&, ws::Opcode) {
            if (!ws) return;
            ws->sendText(R"({"type":"error","message":"ws chat not enabled in this version"})");
        },
        nullptr);

    server.start();
    return 0;
}
