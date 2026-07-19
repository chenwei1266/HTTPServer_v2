#pragma once

#include "../llm/LLMManager.h"
#include "../user/UserManager.h"
#include "../storage/StorageManager.h"
#include "../../HttpServer_v2/include/http/HttpServer.h"
#include <memory>
#include <string>

namespace chatui {
namespace application {

// ChatUI - 应用入口，管理 HTTP 服务和各模块生命周期
class ChatUI {
public:
    struct Config {
        int port = 8088;
        std::string modelsConfigPath = "models.json";

        // 数据库配置
        storage::StorageManager::Config dbConfig;
    };

    ChatUI();
    ~ChatUI();

    ChatUI(const ChatUI&) = delete;
    ChatUI& operator=(const ChatUI&) = delete;

    bool init(const Config& config);  // 初始化各模块
    void run();                       // 启动服务
    void shutdown();                  // 优雅关闭

private:
    void setupRoutes();  // 注册路由

    // ── 路由处理器 ──────────────────────────────────────────
    void handleIndex(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleHealth(const ::http::HttpRequest& req, ::http::HttpResponse* resp);

    // 认证相关
    void handleRegister(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleLogin(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleLogout(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleGetUser(const ::http::HttpRequest& req, ::http::HttpResponse* resp);

    // 对话相关
    void handleListConversations(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleCreateConversation(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleUpdateConversation(const ::http::HttpRequest& req, ::http::HttpResponse* resp);
    void handleDeleteConversation(const ::http::HttpRequest& req, ::http::HttpResponse* resp);

    // 消息相关
    void handleGetMessages(const ::http::HttpRequest& req, ::http::HttpResponse* resp);

    // 聊天 SSE
    void handleChatSSE(const ::http::HttpRequest& req, const sse::SseConnectionPtr& conn);

    // ── 辅助方法 ────────────────────────────────────────────
    std::optional<int> checkAuth(const ::http::HttpRequest& req);  // 从请求中提取并验证用户
    std::string loadHtmlPage(const std::string& path);  // 加载 HTML 文件

    // ── 成员变量 ────────────────────────────────────────────
    Config config_;
    std::unique_ptr<::http::HttpServer> server_;
    std::unique_ptr<storage::StorageManager> storageMgr_;
    std::unique_ptr<llm::LLMManager> llmMgr_;
    std::unique_ptr<user::UserManager> userMgr_;

    bool initialized_ = false;
    std::string htmlContent_;  // 缓存的前端页面
};

} // namespace application
} // namespace chatui
