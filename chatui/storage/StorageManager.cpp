#include "StorageManager.h"
#include <iostream>

// 引入 HttpServer_v2 的数据库连接池
#include "../../HttpServer_v2/include/utils/db/DbConnectionPool.h"

using http::db::DbConnectionPool;

namespace chatui {
namespace storage {

StorageManager::StorageManager() {
    std::cout << "[StorageManager] Created\n";
}

StorageManager::~StorageManager() {
    shutdown();
}

bool StorageManager::init(const Config& config) {
    if (initialized_) {
        std::cerr << "[StorageManager] Already initialized\n";
        return false;
    }

    try {
        // 初始化连接池（使用单例）
        pool_ = &http::db::DbConnectionPool::getInstance();
        pool_->init(config.host, config.user, config.password, config.database, config.poolSize);

        // 创建 Repository 实例
        userRepo_ = std::make_unique<UserRepository>(pool_);
        conversationRepo_ = std::make_unique<ConversationRepository>(pool_);
        messageRepo_ = std::make_unique<MessageRepository>(pool_);

        initialized_ = true;
        std::cout << "[StorageManager] Initialized with pool size: " << config.poolSize << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[StorageManager] Init failed: " << e.what() << "\n";
        return false;
    }
}

void StorageManager::shutdown() {
    if (!initialized_) return;

    userRepo_.reset();
    conversationRepo_.reset();
    messageRepo_.reset();

    pool_ = nullptr;
    initialized_ = false;
    std::cout << "[StorageManager] Shutdown complete\n";
}

UserRepository& StorageManager::getUserRepo() {
    if (!initialized_) {
        throw std::runtime_error("StorageManager not initialized");
    }
    return *userRepo_;
}

ConversationRepository& StorageManager::getConversationRepo() {
    if (!initialized_) {
        throw std::runtime_error("StorageManager not initialized");
    }
    return *conversationRepo_;
}

MessageRepository& StorageManager::getMessageRepo() {
    if (!initialized_) {
        throw std::runtime_error("StorageManager not initialized");
    }
    return *messageRepo_;
}

bool StorageManager::ensureSchema() {
    if (!initialized_) {
        std::cerr << "[StorageManager] Not initialized, cannot ensure schema\n";
        return false;
    }

    auto conn = pool_->getConnection();
    try {
        // 创建 users 表
        std::string createUsers = R"(
            CREATE TABLE IF NOT EXISTS users (
                id INT AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(50) NOT NULL UNIQUE,
                email VARCHAR(100) NOT NULL UNIQUE,
                password_hash VARCHAR(64) NOT NULL,
                salt VARCHAR(32) NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                INDEX idx_email (email),
                INDEX idx_username (username)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )";
        conn->executeUpdate(createUsers);
        std::cout << "[StorageManager] Ensured users table\n";

        // 创建 conversations 表
        std::string createConversations = R"(
            CREATE TABLE IF NOT EXISTS conversations (
                id INT AUTO_INCREMENT PRIMARY KEY,
                user_id INT NOT NULL,
                title VARCHAR(255) NOT NULL DEFAULT 'New Conversation',
                model VARCHAR(100) NOT NULL DEFAULT 'qwen-plus',
                system_prompt TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                INDEX idx_user_id (user_id),
                INDEX idx_updated_at (updated_at),
                FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )";
        conn->executeUpdate(createConversations);
        std::cout << "[StorageManager] Ensured conversations table\n";

        // 创建 messages 表
        std::string createMessages = R"(
            CREATE TABLE IF NOT EXISTS messages (
                id INT AUTO_INCREMENT PRIMARY KEY,
                conversation_id INT NOT NULL,
                role VARCHAR(20) NOT NULL,
                content TEXT NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_conversation_id (conversation_id),
                INDEX idx_created_at (created_at),
                FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )";
        conn->executeUpdate(createMessages);
        std::cout << "[StorageManager] Ensured messages table\n";

        std::cout << "[StorageManager] Schema check complete\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[StorageManager] ensureSchema error: " << e.what() << "\n";
        return false;
    }
}

} // namespace storage
} // namespace chatui
