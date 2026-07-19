#pragma once

#include "Message.h"
#include <vector>

namespace http { namespace db { class DbConnectionPool; } }

namespace chatui {
namespace storage {

class MessageRepository {
public:
    explicit MessageRepository(::http::db::DbConnectionPool* pool);
    ~MessageRepository() = default;

    std::vector<Message> findByConversationId(int conversationId);
    int insert(const Message& message);
    bool insertBatch(const std::vector<Message>& messages);
    bool deleteByConversationId(int conversationId);

private:
    ::http::db::DbConnectionPool* pool_;
};

}
}
