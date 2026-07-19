#include "UserManager.h"
#include "../storage/StorageManager.h"
#include "../utils/SessionManager.h"
#include <openssl/sha.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace chatui {
namespace user {

UserManager::UserManager(storage::StorageManager* storageMgr)
    : storageMgr_(storageMgr) {
    std::cout << "[UserManager] Initialized\n";
}

// ════════════════════════════════════════════════════════════
//  用户管理
// ════════════════════════════════════════════════════════════

std::optional<storage::User> UserManager::getUser(int userId) {
    return storageMgr_->getUserRepo().findById(userId);
}

std::optional<storage::User> UserManager::getUserByEmail(const std::string& email) {
    return storageMgr_->getUserRepo().findByEmail(email);
}

int UserManager::createUser(const std::string& username, const std::string& email, const std::string& password) {
    // 检查邮箱是否已存在
    if (storageMgr_->getUserRepo().findByEmail(email).has_value()) {
        std::cerr << "[UserManager] Email already exists: " << email << "\n";
        return -1;
    }

    // 检查用户名是否已存在
    if (storageMgr_->getUserRepo().findByUsername(username).has_value()) {
        std::cerr << "[UserManager] Username already exists: " << username << "\n";
        return -1;
    }

    // 生成盐值和密码哈希
    std::string salt = generateSalt();
    std::string passwordHash = hashPassword(password, salt);

    storage::User user;
    user.username = username;
    user.email = email;
    user.passwordHash = passwordHash;
    user.salt = salt;
    user.createdAt = std::time(nullptr);
    user.updatedAt = std::time(nullptr);

    int userId = storageMgr_->getUserRepo().insert(user);
    if (userId > 0) {
        std::cout << "[UserManager] Created user: " << username << " (id: " << userId << ")\n";
    }
    return userId;
}

bool UserManager::verifyPassword(const std::string& email, const std::string& password) {
    auto userOpt = storageMgr_->getUserRepo().findByEmail(email);
    if (!userOpt.has_value()) {
        return false;
    }

    const auto& user = userOpt.value();
    std::string computedHash = hashPassword(password, user.salt);
    return computedHash == user.passwordHash;
}

// ════════════════════════════════════════════════════════════
//  鉴权
// ════════════════════════════════════════════════════════════

std::optional<int> UserManager::checkAuth(const std::string& token) {
    // 使用 SessionManager 验证 token
    int userId = utils::SessionManager::getInstance().validateSession(token);
    if (userId > 0) {
        return userId;
    }
    return std::nullopt;
}

// ════════════════════════════════════════════════════════════
//  对话管理
// ════════════════════════════════════════════════════════════

int UserManager::createConversation(int userId, const std::string& model, const std::string& systemPrompt) {
    // 验证用户是否存在
    if (!getUser(userId).has_value()) {
        std::cerr << "[UserManager] User not found: " << userId << "\n";
        return -1;
    }

    storage::Conversation conversation;
    conversation.userId = userId;
    conversation.title = "New Conversation";
    conversation.model = model.empty() ? "qwen-plus" : model;
    conversation.systemPrompt = systemPrompt;
    conversation.createdAt = std::time(nullptr);
    conversation.updatedAt = std::time(nullptr);

    int conversationId = storageMgr_->getConversationRepo().insert(conversation);
    if (conversationId > 0) {
        std::cout << "[UserManager] Created conversation: " << conversationId
                  << " for user: " << userId << "\n";
    }
    return conversationId;
}

std::vector<storage::Conversation> UserManager::listConversations(int userId, int limit) {
    return storageMgr_->getConversationRepo().findByUserId(userId, limit);
}

std::optional<storage::Conversation> UserManager::getConversation(int conversationId) {
    return storageMgr_->getConversationRepo().findById(conversationId);
}

bool UserManager::updateConversationTitle(int conversationId, const std::string& title) {
    return storageMgr_->getConversationRepo().updateTitle(conversationId, title);
}

bool UserManager::deleteConversation(int conversationId) {
    // 先删除消息
    storageMgr_->getMessageRepo().deleteByConversationId(conversationId);
    // 再删除对话
    return storageMgr_->getConversationRepo().deleteById(conversationId);
}

// ════════════════════════════════════════════════════════════
//  消息管理
// ════════════════════════════════════════════════════════════

std::vector<storage::Message> UserManager::getMessages(int conversationId) {
    return storageMgr_->getMessageRepo().findByConversationId(conversationId);
}

bool UserManager::appendMessage(int conversationId, const std::string& role, const std::string& content) {
    storage::Message message;
    message.conversationId = conversationId;
    message.role = role;
    message.content = content;
    message.createdAt = std::time(nullptr);

    int messageId = storageMgr_->getMessageRepo().insert(message);
    if (messageId > 0) {
        // 更新对话的 updatedAt 时间戳
        storageMgr_->getConversationRepo().touch(conversationId);
        return true;
    }
    return false;
}

// ════════════════════════════════════════════════════════════
//  上下文构建
// ════════════════════════════════════════════════════════════

std::optional<Context> UserManager::buildContext(int conversationId) {
    auto conversationOpt = getConversation(conversationId);
    if (!conversationOpt.has_value()) {
        return std::nullopt;
    }

    const auto& conversation = conversationOpt.value();
    Context context;
    context.conversationId = conversationId;
    context.model = conversation.model;
    context.systemPrompt = conversation.systemPrompt;

    // 加载历史消息
    auto messages = getMessages(conversationId);
    for (const auto& msg : messages) {
        context.addMessage(msg.role, msg.content);
    }

    std::cout << "[UserManager] Built context for conversation " << conversationId
              << " with " << messages.size() << " messages\n";
    return context;
}

// ════════════════════════════════════════════════════════════
//  密码处理
// ════════════════════════════════════════════════════════════

std::string UserManager::hashPassword(const std::string& password, const std::string& salt) {
    std::string data = salt + password;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

std::string UserManager::generateSalt() {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

    std::string salt;
    salt.reserve(16);
    for (int i = 0; i < 16; ++i) {
        salt += charset[dis(gen)];
    }
    return salt;
}

} // namespace user
} // namespace chatui
