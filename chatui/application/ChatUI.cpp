#include "ChatUI.h"
#include "../utils/JsonParser.h"
#include "../utils/SessionManager.h"
#include <iostream>
#include <fstream>
#include <sstream>

namespace chatui {
namespace application {

ChatUI::ChatUI() {
    std::cout << "[ChatUI] Created\n";
}

ChatUI::~ChatUI() {
    shutdown();
}

bool ChatUI::init(const Config& config) {
    if (initialized_) {
        std::cerr << "[ChatUI] Already initialized\n";
        return false;
    }

    config_ = config;

    // 1. 初始化存储层
    storageMgr_ = std::make_unique<storage::StorageManager>();
    if (!storageMgr_->init(config.dbConfig)) {
        std::cerr << "[ChatUI] Failed to initialize StorageManager\n";
        return false;
    }

    // 确保数据库表结构
    if (!storageMgr_->ensureSchema()) {
        std::cerr << "[ChatUI] Failed to ensure database schema\n";
        return false;
    }

    // 2. 初始化用户管理器
    userMgr_ = std::make_unique<user::UserManager>(storageMgr_.get());

    // 3. 初始化 LLM 管理器
    llmMgr_ = std::make_unique<llm::LLMManager>();
    if (!llmMgr_->loadConfig(config.modelsConfigPath)) {
        std::cerr << "[ChatUI] Warning: Failed to load models config from "
                  << config.modelsConfigPath << ", using defaults\n";
    }

    // 4. 初始化 HTTP 服务器
    server_ = std::make_unique<::http::HttpServer>(config.port, "ChatUI_v2", false);
    setupRoutes();

    // 5. 加载前端页面
    htmlContent_ = loadHtmlPage("chatui.html");
    if (htmlContent_.empty()) {
        std::cerr << "[ChatUI] Warning: Failed to load chatui.html\n";
    }

    initialized_ = true;
    std::cout << "[ChatUI] Initialized successfully on port " << config.port << "\n";
    return true;
}

void ChatUI::run() {
    if (!initialized_) {
        std::cerr << "[ChatUI] Not initialized, cannot run\n";
        return;
    }

    std::cout << "[ChatUI] Starting server...\n";
    server_->start();
}

void ChatUI::shutdown() {
    if (!initialized_) return;

    std::cout << "[ChatUI] Shutting down...\n";

    if (llmMgr_) {
        llmMgr_->shutdown();
    }

    if (storageMgr_) {
        storageMgr_->shutdown();
    }

    initialized_ = false;
    std::cout << "[ChatUI] Shutdown complete\n";
}

// ════════════════════════════════════════════════════════════
//  路由设置
// ════════════════════════════════════════════════════════════

void ChatUI::setupRoutes() {
    // 页面与健康检查
    server_->addRoute(::http::HttpRequest::kGet, "/", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleIndex(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kGet, "/api/health", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleHealth(req, resp);
    });

    // 认证接口
    server_->addRoute(::http::HttpRequest::kPost, "/api/auth/register", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleRegister(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kPost, "/api/auth/login", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleLogin(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kPost, "/api/auth/logout", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleLogout(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kGet, "/api/users/me", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleGetUser(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kGet, "/api/auth/me", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleGetUser(req, resp);  // 兼容旧接口
    });

    // 对话接口
    server_->addRoute(::http::HttpRequest::kGet, "/api/conversations", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleListConversations(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kPost, "/api/conversations", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleCreateConversation(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kPatch, "/api/conversations/:id", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleUpdateConversation(req, resp);
    });
    server_->addRoute(::http::HttpRequest::kPut, "/api/conversations/:id", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleUpdateConversation(req, resp);  // 兼容 PUT
    });
    server_->addRoute(::http::HttpRequest::kDelete, "/api/conversations/:id", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleDeleteConversation(req, resp);
    });

    // 消息接口
    server_->addRoute(::http::HttpRequest::kGet, "/api/conversations/:id/messages", [this](const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
        handleGetMessages(req, resp);
    });

    // SSE 流式聊天
    server_->Sse("/api/chat/sse", [this](const ::http::HttpRequest& req, const sse::SseConnectionPtr& conn) {
        handleChatSSE(req, conn);
    });

    std::cout << "[ChatUI] Routes registered\n";
}

// ════════════════════════════════════════════════════════════
//  路由处理器实现
// ════════════════════════════════════════════════════════════

void ChatUI::handleIndex(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    if (htmlContent_.empty()) {
        resp->setStatusCode(::http::HttpResponse::k500InternalServerError);
        resp->setBody("Frontend not available");
    } else {
        resp->setStatusCode(::http::HttpResponse::k200Ok);
        resp->addHeader("Content-Type", "text/html; charset=utf-8");
        resp->setBody(htmlContent_);
    }
}

void ChatUI::handleHealth(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(R"({"status":"ok"})");
}

void ChatUI::handleRegister(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    std::string body = req.getBody();

    std::string username = utils::JsonParser::getString(body, "username");
    std::string email = utils::JsonParser::getString(body, "email");
    std::string password = utils::JsonParser::getString(body, "password");

    if (username.empty() || email.empty() || password.empty()) {
        resp->setStatusCode(::http::HttpResponse::k400BadRequest);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Missing required fields"})");
        return;
    }

    int userId = userMgr_->createUser(username, email, password);
    if (userId < 0) {
        resp->setStatusCode(::http::HttpResponse::k409Conflict);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"User already exists"})");
        return;
    }

    // 创建 session
    std::string token = utils::SessionManager::getInstance().createSession(userId);

    std::ostringstream oss;
    oss << R"({"success":true,"user_id":)" << userId
        << R"(,"token":")" << token << "\""
        << R"(,"username":")" << utils::JsonParser::escape(username) << "\""
        << "}";

    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(oss.str());
}

void ChatUI::handleLogin(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    std::string body = req.getBody();

    std::string email = utils::JsonParser::getString(body, "email");
    std::string password = utils::JsonParser::getString(body, "password");

    if (email.empty() || password.empty()) {
        resp->setStatusCode(::http::HttpResponse::k400BadRequest);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Missing required fields"})");
        return;
    }

    // 验证密码
    if (!userMgr_->verifyPassword(email, password)) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Invalid email or password"})");
        return;
    }

    // 获取用户信息
    auto userOpt = userMgr_->getUserByEmail(email);
    if (!userOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Invalid email or password"})");
        return;
    }

    const auto& user = userOpt.value();

    // 创建 session
    std::string token = utils::SessionManager::getInstance().createSession(user.id);

    std::ostringstream oss;
    oss << R"({"success":true,"user_id":)" << user.id
        << R"(,"token":")" << token << "\""
        << R"(,"username":")" << utils::JsonParser::escape(user.username) << "\""
        << "}";

    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(oss.str());
}

void ChatUI::handleLogout(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    std::string auth = req.getHeader("Authorization");
    if (!auth.empty()) {
        // 移除 "Bearer " 前缀
        if (auth.find("Bearer ") == 0) {
            auth = auth.substr(7);
        }
        utils::SessionManager::getInstance().removeSession(auth);
    }

    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(R"({"success":true})");
}

void ChatUI::handleGetUser(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Unauthorized"})");
        return;
    }

    auto userOpt = userMgr_->getUser(userIdOpt.value());
    if (!userOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k404NotFound);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"User not found"})");
        return;
    }

    const auto& user = userOpt.value();
    std::ostringstream oss;
    oss << R"({"id":)" << user.id
        << R"(,"username":")" << user.username << "\""
        << R"(,"email":")" << user.email << "\""
        << "}";

    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(oss.str());
}

void ChatUI::handleListConversations(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Unauthorized"})");
        return;
    }

    auto conversations = userMgr_->listConversations(userIdOpt.value());

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < conversations.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& conv = conversations[i];
        oss << R"({"id":)" << conv.id
            << R"(,"title":")" << conv.title << "\""
            << R"(,"model":")" << conv.model << "\""
            << R"(,"updated_at":)" << conv.updatedAt
            << "}";
    }
    oss << "]";

    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(oss.str());
}

void ChatUI::handleCreateConversation(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Unauthorized"})");
        return;
    }

    // TODO: 解析请求体获取 model 和 system_prompt
    int conversationId = userMgr_->createConversation(userIdOpt.value());
    if (conversationId < 0) {
        resp->setStatusCode(::http::HttpResponse::k500InternalServerError);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Failed to create conversation"})");
        return;
    }

    std::ostringstream oss;
    oss << R"({"id":)" << conversationId << "}";
    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(oss.str());
}

void ChatUI::handleUpdateConversation(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Unauthorized"})");
        return;
    }

    // 从路径参数获取 conversation_id
    std::string idStr = req.getPathParameters("id");
    int conversationId = std::atoi(idStr.c_str());
    if (conversationId <= 0) {
        resp->setStatusCode(::http::HttpResponse::k400BadRequest);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Invalid conversation id"})");
        return;
    }

    // 解析请求体
    std::string body = req.getBody();
    std::string title = utils::JsonParser::getString(body, "title");

    if (title.empty()) {
        resp->setStatusCode(::http::HttpResponse::k400BadRequest);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Title is required"})");
        return;
    }

    if (userMgr_->updateConversationTitle(conversationId, title)) {
        resp->setStatusCode(::http::HttpResponse::k200Ok);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"success":true})");
    } else {
        resp->setStatusCode(::http::HttpResponse::k500InternalServerError);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Failed to update conversation"})");
    }
}

void ChatUI::handleDeleteConversation(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Unauthorized"})");
        return;
    }

    std::string idStr = req.getPathParameters("id");
    int conversationId = std::atoi(idStr.c_str());
    if (conversationId <= 0) {
        resp->setStatusCode(::http::HttpResponse::k400BadRequest);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Invalid conversation id"})");
        return;
    }

    if (userMgr_->deleteConversation(conversationId)) {
        resp->setStatusCode(::http::HttpResponse::k200Ok);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"success":true})");
    } else {
        resp->setStatusCode(::http::HttpResponse::k500InternalServerError);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Failed to delete conversation"})");
    }
}

void ChatUI::handleGetMessages(const ::http::HttpRequest& req, ::http::HttpResponse* resp) {
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        resp->setStatusCode(::http::HttpResponse::k401Unauthorized);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Unauthorized"})");
        return;
    }

    std::string idStr = req.getPathParameters("id");
    int conversationId = std::atoi(idStr.c_str());
    if (conversationId <= 0) {
        resp->setStatusCode(::http::HttpResponse::k400BadRequest);
        resp->addHeader("Content-Type", "application/json");
        resp->setBody(R"({"error":"Invalid conversation id"})");
        return;
    }

    auto messages = userMgr_->getMessages(conversationId);

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& msg = messages[i];
        oss << R"({"id":)" << msg.id
            << R"(,"role":")" << msg.role << "\""
            << R"(,"content":")" << utils::JsonParser::escape(msg.content) << "\""
            << R"(,"created_at":)" << msg.createdAt
            << "}";
    }
    oss << "]";

    resp->setStatusCode(::http::HttpResponse::k200Ok);
    resp->addHeader("Content-Type", "application/json");
    resp->setBody(oss.str());
}

void ChatUI::handleChatSSE(const ::http::HttpRequest& req, const sse::SseConnectionPtr& conn) {
    // 1. 鉴权
    auto userIdOpt = checkAuth(req);
    if (!userIdOpt.has_value()) {
        conn->send("data: {\"error\":\"Unauthorized\"}\n\n");
        conn->close();
        return;
    }

    // 2. 解析请求（从查询参数获取）
    std::string conversationIdStr = req.getQueryParameters("conversation_id");
    std::string userMessage = req.getQueryParameters("message");

    int conversationId = conversationIdStr.empty() ? -1 : std::atoi(conversationIdStr.c_str());

    if (userMessage.empty()) {
        conn->send("data: {\"error\":\"Message is required\"}\n\n");
        conn->close();
        return;
    }

    // 3. 构建上下文
    user::Context context;
    if (conversationId > 0) {
        auto ctxOpt = userMgr_->buildContext(conversationId);
        if (ctxOpt.has_value()) {
            context = ctxOpt.value();
        } else {
            conn->send("data: {\"error\":\"Conversation not found\"}\n\n");
            conn->close();
            return;
        }
    } else {
        // 创建新对话
        conversationId = userMgr_->createConversation(userIdOpt.value());
        if (conversationId < 0) {
            conn->send("data: {\"error\":\"Failed to create conversation\"}\n\n");
            conn->close();
            return;
        }
        context.conversationId = conversationId;
        context.model = "qwen-plus";
    }

    // 4. 保存用户消息
    userMgr_->appendMessage(conversationId, "user", userMessage);
    context.addMessage("user", userMessage);

    // 5. 发送初始事件
    std::ostringstream initMsg;
    initMsg << "data: {\"type\":\"start\",\"conversation_id\":" << conversationId << "}\n\n";
    conn->send(initMsg.str());

    // 6. 准备 LLM 请求上下文
    llm::RequestContext llmContext;
    llmContext.conversationId = std::to_string(conversationId);
    llmContext.userId = std::to_string(userIdOpt.value());
    llmContext.model = context.model;
    llmContext.messages = context.messages;
    llmContext.systemPrompt = context.systemPrompt;

    // 7. 发起异步推理
    std::string requestId = llmMgr_->get(llmContext,
        // onEvent - 流式 token 回调
        [conn](const llm::StreamEvent& event) {
            if (event.type == llm::StreamEvent::TOKEN) {
                std::ostringstream oss;
                oss << "data: {\"type\":\"token\",\"content\":\""
                    << utils::JsonParser::escape(event.content) << "\"}\n\n";
                conn->send(oss.str());
            }
        },
        // onDone - 完成回调
        [conn, conversationId, this](const llm::InferenceResult& result) {
            if (result.success) {
                // 保存助手消息
                userMgr_->appendMessage(conversationId, "assistant", result.fullContent);

                std::ostringstream oss;
                oss << "data: {\"type\":\"done\",\"success\":true,"
                    << "\"input_tokens\":" << result.inputTokens << ","
                    << "\"output_tokens\":" << result.outputTokens << "}\n\n";
                conn->send(oss.str());
            } else {
                std::ostringstream oss;
                oss << "data: {\"type\":\"error\",\"error\":\""
                    << utils::JsonParser::escape(result.error) << "\"}\n\n";
                conn->send(oss.str());
            }
            conn->close();
        }
    );

    std::cout << "[ChatUI] Started SSE chat request: " << requestId << "\n";
}

// ════════════════════════════════════════════════════════════
//  辅助方法
// ════════════════════════════════════════════════════════════

std::optional<int> ChatUI::checkAuth(const ::http::HttpRequest& req) {
    // 从 Authorization header 提取 token
    std::string auth = req.getHeader("Authorization");
    if (auth.empty()) {
        // 尝试从 Cookie 获取
        std::string cookie = req.getHeader("Cookie");
        if (!cookie.empty()) {
            // 解析 Cookie: token=xxx
            auto pos = cookie.find("token=");
            if (pos != std::string::npos) {
                pos += 6; // strlen("token=")
                auto endPos = cookie.find(';', pos);
                auth = cookie.substr(pos, endPos == std::string::npos ? std::string::npos : endPos - pos);
            }
        }
    } else {
        // 移除 "Bearer " 前缀
        if (auth.find("Bearer ") == 0) {
            auth = auth.substr(7);
        }
    }

    if (auth.empty()) {
        return std::nullopt;
    }

    return userMgr_->checkAuth(auth);
}

std::string ChatUI::loadHtmlPage(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "[ChatUI] Failed to open " << path << "\n";
        return "";
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

} // namespace application
} // namespace chatui
