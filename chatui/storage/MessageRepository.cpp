#include "MessageRepository.h"
#include "../../HttpServer_v2/include/utils/db/DbConnectionPool.h"
#include "../../HttpServer_v2/include/utils/db/DbConnection.h"
#include <iostream>
#include <memory>
using http::db::DbConnectionPool;
using http::db::DbConnection;

namespace chatui {
namespace storage {

MessageRepository::MessageRepository(DbConnectionPool* pool) : pool_(pool) {
}

std::vector<Message> MessageRepository::findByConversationId(int conversationId) {
    auto conn = pool_->getConnection();
    std::vector<Message> messages;
    try {
        std::string sql = "SELECT id, conversation_id, role, content, UNIX_TIMESTAMP(created_at) as created_at FROM messages WHERE conversation_id = ? ORDER BY created_at ASC";
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery(sql, conversationId));

        while (rs->next()) {
            Message msg;
            msg.id = rs->getInt("id");
            msg.conversationId = rs->getInt("conversation_id");
            msg.role = rs->getString("role");
            msg.content = rs->getString("content");
            msg.createdAt = rs->getInt64("created_at");
            messages.push_back(msg);
        }
        
    } catch (const std::exception& e) {
        
        std::cerr << "[MessageRepository] findByConversationId error: " << e.what() << "\n";
    }
    return messages;
}

int MessageRepository::insert(const Message& message) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "INSERT INTO messages (conversation_id, role, content, created_at) VALUES (?, ?, ?, FROM_UNIXTIME(?))";
        conn->executeUpdate(sql, message.conversationId, message.role, message.content, message.createdAt);

        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery("SELECT LAST_INSERT_ID() as id"));
        int messageId = -1;
        if (rs->next()) {
            messageId = rs->getInt("id");
        }
        
        return messageId;
    } catch (const std::exception& e) {
        
        std::cerr << "[MessageRepository] insert error: " << e.what() << "\n";
        return -1;
    }
}

bool MessageRepository::insertBatch(const std::vector<Message>& messages) {
    if (messages.empty()) return true;

    auto conn = pool_->getConnection();
    try {
        // 构建批量插入 SQL
        std::string sql = "INSERT INTO messages (conversation_id, role, content, created_at) VALUES ";
        for (size_t i = 0; i < messages.size(); ++i) {
            if (i > 0) sql += ",";
            sql += "(?, ?, ?, FROM_UNIXTIME(?))";
        }

        // 注意：当前的 DbConnection 不支持可变参数批量绑定
        // 所以这里回退到逐条插入
        for (const auto& msg : messages) {
            if (insert(msg) < 0) {
                
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        
        std::cerr << "[MessageRepository] insertBatch error: " << e.what() << "\n";
        return false;
    }
}

bool MessageRepository::deleteByConversationId(int conversationId) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "DELETE FROM messages WHERE conversation_id = ?";
        conn->executeUpdate(sql, conversationId);
        
        return true;
    } catch (const std::exception& e) {
        
        std::cerr << "[MessageRepository] deleteByConversationId error: " << e.what() << "\n";
        return false;
    }
}

} // namespace storage
} // namespace chatui
