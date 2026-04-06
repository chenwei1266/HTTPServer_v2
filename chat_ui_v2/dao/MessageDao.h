#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "utils/db/DbConnectionPool.h"

namespace chatui_v2 {
namespace dao {

struct ChatMessage
{
    int64_t id = 0;
    int64_t conversationId = 0;
    std::string role;
    std::string content;
    int64_t createdAt = 0;
};

class MessageDao
{
public:
    static int64_t insert(int64_t conversationId,
                          const std::string& role,
                          const std::string& content)
    {
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        conn->executeUpdate(
            "INSERT INTO messages (conversation_id, role, content) VALUES (?, ?, ?)",
            conversationId, role, content);
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery("SELECT LAST_INSERT_ID() AS id"));
        if (rs && rs->next()) return rs->getInt64("id");
        return 0;
    }

    static std::vector<ChatMessage> listByConversation(int64_t conversationId)
    {
        std::vector<ChatMessage> out;
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        std::unique_ptr<sql::ResultSet> rs(
            conn->executeQuery(
                "SELECT id, conversation_id, role, content, UNIX_TIMESTAMP(created_at) AS created_at_ts "
                "FROM messages WHERE conversation_id = ? ORDER BY created_at DESC",
                conversationId));

        while (rs && rs->next())
        {
            ChatMessage m;
            m.id = rs->getInt64("id");
            m.conversationId = rs->getInt64("conversation_id");
            m.role = rs->getString("role");
            m.content = rs->getString("content");
            m.createdAt = rs->getInt64("created_at_ts");
            out.push_back(std::move(m));
        }
        return out;
    }
};

} // namespace dao
} // namespace chatui_v2
