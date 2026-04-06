#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/AIConfig.h"
#include "ai/AIFactory.h"
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
#include "session/SessionManager.h"
#include "session/SessionStorage.h"
#include "utils/db/DbConnectionPool.h"
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

    // 索引/约束幂等处理：已存在时报错则忽略
    try { conn->executeUpdate("ALTER TABLE users ADD UNIQUE KEY uk_users_email (email)"); } catch (...) {}
    try { conn->executeUpdate("ALTER TABLE users ADD UNIQUE KEY uk_users_username (username)"); } catch (...) {}
}

std::string env(const char* key, const std::string& def = "")
{
    const char* v = std::getenv(key);
    return (v && v[0]) ? std::string(v) : def;
}

std::string inferProvider(const std::string& modelId,
                          const std::string& fallbackProvider)
{
    std::string m = modelId;
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (m.empty()) return fallbackProvider;
    if (m.find("claude") != std::string::npos) return "claude";
    if (m == "qwen3-coder-next") return "local";
    if (m.find("qwen") != std::string::npos) return "qwen";
    if (m.find("ernie") != std::string::npos || m.find("wenxin") != std::string::npos) return "wenxin";
    if (m.find("doubao") != std::string::npos || m.find("ep-") == 0) return "doubao";

    if (ai::AIFactory::instance().hasModel(modelId)) return modelId;
    return fallbackProvider;
}

std::string sseJson(const std::string& type, const std::string& contentField, const std::string& content)
{
    return std::string("{\"type\":\"") + type + "\",\"" + contentField + "\":\""
         + chatui_v2::json::escape(content) + "\"}";
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

    auto storage = std::make_unique<http::session::MemorySessionStorage>();
    auto sm = std::make_unique<http::session::SessionManager>(std::move(storage));
    auto* smPtr = sm.get();
    server.setSessionManager(std::move(sm));

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

    server.Sse("/api/chat/sse", [smPtr, aiConfig](const http::HttpRequest& req, const sse::SseConnectionPtr& sse) {
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

        const std::string body = req.getBody();
        int64_t convId = chatui_v2::json::extractInt64(body, "conversation_id", 0);
        const std::string requestedModel = chatui_v2::json::extractString(body, "model");
        const std::string systemPrompt = chatui_v2::json::extractString(body, "system_prompt");
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
            chatui_v2::dao::MessageDao::insert(convId, "user", lastUserMsg);
        }

        const std::string provider = inferProvider(requestedModel, aiConfig.defaultModel());
        ai::ModelConfig cfg = aiConfig.getConfig(provider);
        if (cfg.model.empty()) cfg.model = requestedModel;
        auto strategy = ai::AIFactory::instance().tryCreateModel(provider, cfg);
        if (!strategy)
        {
            sse->send(R"({"type":"error","message":"unknown model"})", "error");
            sse->send(R"({"type":"done"})", "done");
            sse->close();
            return;
        }

        auto strategyPtr = std::shared_ptr<ai::AIStrategy>(std::move(strategy));
        sse->send(std::string("{\"type\":\"meta\",\"provider\":\"")
                  + chatui_v2::json::escape(strategyPtr->getProviderName())
                  + "\",\"model\":\"" + chatui_v2::json::escape(strategyPtr->getModelName()) + "\"}", "meta");

        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        std::string full;

        auto finish = [&](const std::string& maybeErr) {
            {
                std::lock_guard<std::mutex> lk(mu);
                if (!done) done = true;
            }
            cv.notify_one();
            if (!maybeErr.empty())
            {
                sse->send(std::string("{\"type\":\"error\",\"message\":\"") + chatui_v2::json::escape(maybeErr) + "\"}", "error");
            }
            sse->send(R"({"type":"done"})", "done");
        };

        strategyPtr->sendStreamMsg(
            messages,
            [&](const std::string& token) {
                if (!sse->connected()) return;
                full += token;
                sse->send(std::string("{\"type\":\"token\",\"content\":\"") + chatui_v2::json::escape(token) + "\"}");
            },
            [&]() { finish(""); },
            [&](const std::string& err) { finish(err); });

        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&]() { return done; });
        }

        if (convId > 0 && !full.empty())
        {
            chatui_v2::dao::MessageDao::insert(convId, "assistant", full);
            chatui_v2::dao::ConversationDao::touch(convId);
        }

        sse->close();
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
