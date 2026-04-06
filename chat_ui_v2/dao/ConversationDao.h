#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "utils/db/DbConnectionPool.h"

namespace chatui_v2 {
namespace dao {

struct Conversation
{
    int64_t id = 0;
    int64_t userId = 0;
    std::string title;
    std::string model;
    std::string systemPrompt;
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

class ConversationDao
{
public:
    static int64_t create(int64_t userId,
                          const std::string& title,
                          const std::string& model,
                          const std::string& systemPrompt)
    {
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        conn->executeUpdate(
            "INSERT INTO conversations (user_id, title, model, system_prompt) VALUES (?, ?, ?, ?)",
            userId, title, model, systemPrompt);

        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery("SELECT LAST_INSERT_ID() AS id"));
        if (rs && rs->next()) return rs->getInt64("id");
        return 0;
    }

    static Conversation findById(int64_t conversationId, int64_t userId)
    {
        Conversation c;
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        std::unique_ptr<sql::ResultSet> rs(
            conn->executeQuery(
                "SELECT id, user_id, title, model, system_prompt, "
                "UNIX_TIMESTAMP(created_at) AS created_at_ts, "
                "UNIX_TIMESTAMP(updated_at) AS updated_at_ts "
                "FROM conversations WHERE id = ? AND user_id = ?",
                conversationId, userId));

        if (rs && rs->next())
        {
            c.id = rs->getInt64("id");
            c.userId = rs->getInt64("user_id");
            c.title = rs->getString("title");
            c.model = rs->getString("model");
            c.systemPrompt = rs->getString("system_prompt");
            c.createdAt = rs->getInt64("created_at_ts");
            c.updatedAt = rs->getInt64("updated_at_ts");
        }
        return c;
    }

    static std::vector<Conversation> listByUser(int64_t userId)
    {
        std::vector<Conversation> out;
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        std::unique_ptr<sql::ResultSet> rs(
            conn->executeQuery(
                "SELECT id, user_id, title, model, system_prompt, "
                "UNIX_TIMESTAMP(created_at) AS created_at_ts, "
                "UNIX_TIMESTAMP(updated_at) AS updated_at_ts "
                "FROM conversations WHERE user_id = ? ORDER BY updated_at DESC",
                userId));

        while (rs && rs->next())
        {
            Conversation c;
            c.id = rs->getInt64("id");
            c.userId = rs->getInt64("user_id");
            c.title = rs->getString("title");
            c.model = rs->getString("model");
            c.systemPrompt = rs->getString("system_prompt");
            c.createdAt = rs->getInt64("created_at_ts");
            c.updatedAt = rs->getInt64("updated_at_ts");
            out.push_back(std::move(c));
        }
        return out;
    }

    static bool updateTitle(int64_t conversationId, int64_t userId, const std::string& title)
    {
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        int affected = conn->executeUpdate(
            "UPDATE conversations SET title = ? WHERE id = ? AND user_id = ?",
            title, conversationId, userId);
        return affected > 0;
    }

    static bool remove(int64_t conversationId, int64_t userId)
    {
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        int affected = conn->executeUpdate(
            "DELETE FROM conversations WHERE id = ? AND user_id = ?",
            conversationId, userId);
        return affected > 0;
    }

    static void touch(int64_t conversationId)
    {
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        conn->executeUpdate("UPDATE conversations SET updated_at = NOW() WHERE id = ?", conversationId);
    }
};

} // namespace dao
} // namespace chatui_v2
