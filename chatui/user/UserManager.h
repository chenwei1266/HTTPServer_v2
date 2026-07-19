#pragma once

#include "../storage/User.h"
#include "../storage/Conversation.h"
#include "../storage/Message.h"
#include "Context.h"
#include <memory>
#include <optional>
#include <vector>

namespace chatui {
namespace storage {
    class StorageManager;  // 前向声明
}

namespace user {

// UserManager - 用户与对话管理门面
class UserManager {
public:
    explicit UserManager(storage::StorageManager* storageMgr);
    ~UserManager() = default;

    UserManager(const UserManager&) = delete;
    UserManager& operator=(const UserManager&) = delete;

    // ── 用户管理 ────────────────────────────────────────────
    std::optional<storage::User> getUser(int userId);
    std::optional<storage::User> getUserByEmail(const std::string& email);
    int createUser(const std::string& username, const std::string& email, const std::string& password);  // 返回 userId
    bool verifyPassword(const std::string& email, const std::string& password);  // 验证密码

    // ── 鉴权 ────────────────────────────────────────────────
    std::optional<int> checkAuth(const std::string& token);  // 从 token 解析出 userId

    // ── 对话管理 ────────────────────────────────────────────
    int createConversation(int userId, const std::string& model = "", const std::string& systemPrompt = "");  // 返回 conversationId
    std::vector<storage::Conversation> listConversations(int userId, int limit = 50);
    std::optional<storage::Conversation> getConversation(int conversationId);
    bool updateConversationTitle(int conversationId, const std::string& title);
    bool deleteConversation(int conversationId);

    // ── 消息管理 ────────────────────────────────────────────
    std::vector<storage::Message> getMessages(int conversationId);
    bool appendMessage(int conversationId, const std::string& role, const std::string& content);  // 追加单条消息并更新会话时间戳

    // ── 上下文构建 ──────────────────────────────────────────
    std::optional<Context> buildContext(int conversationId);  // 从数据库加载并构建对话上下文

private:
    std::string hashPassword(const std::string& password, const std::string& salt);
    std::string generateSalt();

    storage::StorageManager* storageMgr_;
};

} // namespace user
} // namespace chatui
