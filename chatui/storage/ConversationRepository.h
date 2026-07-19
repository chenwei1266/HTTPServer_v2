#pragma once

#include "Conversation.h"
#include <vector>
#include <optional>

// 前向声明
namespace http { namespace db { class DbConnectionPool; } }

namespace chatui {
namespace storage {

class ConversationRepository {
public:
    explicit ConversationRepository(::http::db::DbConnectionPool* pool);
    ~ConversationRepository() = default;

    std::optional<Conversation> findById(int conversationId);
    std::vector<Conversation> findByUserId(int userId, int limit = 50);  // 按 updatedAt 降序

    int insert(const Conversation& conversation);  // 返回新插入的 conversationId
    bool update(const Conversation& conversation);
    bool updateTitle(int conversationId, const std::string& title);
    bool touch(int conversationId);  // 更新 updatedAt 时间戳
    bool deleteById(int conversationId);

private:
    ::http::db::DbConnectionPool* pool_;
};

} // namespace storage
} // namespace chatui
