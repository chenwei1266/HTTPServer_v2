#include "ConversationRepository.h"
#include "../../HttpServer_v2/include/utils/db/DbConnectionPool.h"
#include "../../HttpServer_v2/include/utils/db/DbConnection.h"
#include <iostream>
#include <memory>
using http::db::DbConnectionPool;
using http::db::DbConnection;

namespace chatui {
namespace storage {

ConversationRepository::ConversationRepository(DbConnectionPool* pool) : pool_(pool) {
}

std::optional<Conversation> ConversationRepository::findById(int conversationId) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "SELECT id, user_id, title, model, system_prompt, UNIX_TIMESTAMP(created_at) as created_at, UNIX_TIMESTAMP(updated_at) as updated_at FROM conversations WHERE id = ?";
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery(sql, conversationId));

        if (rs->next()) {
            Conversation conv;
            conv.id = rs->getInt("id");
            conv.userId = rs->getInt("user_id");
            conv.title = rs->getString("title");
            conv.model = rs->getString("model");
            conv.systemPrompt = rs->getString("system_prompt");
            conv.createdAt = rs->getInt64("created_at");
            conv.updatedAt = rs->getInt64("updated_at");
            
            return conv;
        }
        
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] findById error: " << e.what() << "\n";
    }
    return std::nullopt;
}

std::vector<Conversation> ConversationRepository::findByUserId(int userId, int limit) {
    auto conn = pool_->getConnection();
    std::vector<Conversation> conversations;
    try {
        std::string sql = "SELECT id, user_id, title, model, system_prompt, UNIX_TIMESTAMP(created_at) as created_at, UNIX_TIMESTAMP(updated_at) as updated_at FROM conversations WHERE user_id = ? ORDER BY updated_at DESC LIMIT ?";
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery(sql, userId, limit));

        while (rs->next()) {
            Conversation conv;
            conv.id = rs->getInt("id");
            conv.userId = rs->getInt("user_id");
            conv.title = rs->getString("title");
            conv.model = rs->getString("model");
            conv.systemPrompt = rs->getString("system_prompt");
            conv.createdAt = rs->getInt64("created_at");
            conv.updatedAt = rs->getInt64("updated_at");
            conversations.push_back(conv);
        }
        
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] findByUserId error: " << e.what() << "\n";
    }
    return conversations;
}

int ConversationRepository::insert(const Conversation& conversation) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "INSERT INTO conversations (user_id, title, model, system_prompt, created_at, updated_at) VALUES (?, ?, ?, ?, FROM_UNIXTIME(?), FROM_UNIXTIME(?))";
        conn->executeUpdate(sql, conversation.userId, conversation.title, conversation.model, conversation.systemPrompt, conversation.createdAt, conversation.updatedAt);

        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery("SELECT LAST_INSERT_ID() as id"));
        int conversationId = -1;
        if (rs->next()) {
            conversationId = rs->getInt("id");
        }
        
        return conversationId;
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] insert error: " << e.what() << "\n";
        return -1;
    }
}

bool ConversationRepository::update(const Conversation& conversation) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "UPDATE conversations SET title = ?, model = ?, system_prompt = ?, updated_at = FROM_UNIXTIME(?) WHERE id = ?";
        int affected = conn->executeUpdate(sql, conversation.title, conversation.model, conversation.systemPrompt, conversation.updatedAt, conversation.id);
        
        return affected > 0;
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] update error: " << e.what() << "\n";
        return false;
    }
}

bool ConversationRepository::updateTitle(int conversationId, const std::string& title) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "UPDATE conversations SET title = ?, updated_at = NOW() WHERE id = ?";
        int affected = conn->executeUpdate(sql, title, conversationId);
        
        return affected > 0;
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] updateTitle error: " << e.what() << "\n";
        return false;
    }
}

bool ConversationRepository::touch(int conversationId) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "UPDATE conversations SET updated_at = NOW() WHERE id = ?";
        int affected = conn->executeUpdate(sql, conversationId);
        
        return affected > 0;
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] touch error: " << e.what() << "\n";
        return false;
    }
}

bool ConversationRepository::deleteById(int conversationId) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "DELETE FROM conversations WHERE id = ?";
        int affected = conn->executeUpdate(sql, conversationId);
        
        return affected > 0;
    } catch (const std::exception& e) {
        
        std::cerr << "[ConversationRepository] deleteById error: " << e.what() << "\n";
        return false;
    }
}

} // namespace storage
} // namespace chatui
