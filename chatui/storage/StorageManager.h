#pragma once

#include "UserRepository.h"
#include "ConversationRepository.h"
#include "MessageRepository.h"
#include <memory>
#include <string>

namespace http { namespace db { class DbConnectionPool; } }

namespace chatui {
namespace storage {

class StorageManager {
public:
    struct Config {
        std::string host = "127.0.0.1";
        std::string user = "root";
        std::string password = "123456";
        std::string database = "chat_app";
        int poolSize = 10;
    };

    StorageManager();
    ~StorageManager();

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    bool init(const Config& config);
    void shutdown();

    UserRepository& getUserRepo();
    ConversationRepository& getConversationRepo();
    MessageRepository& getMessageRepo();

    bool ensureSchema();

private:
    ::http::db::DbConnectionPool* pool_ = nullptr;
    std::unique_ptr<UserRepository> userRepo_;
    std::unique_ptr<ConversationRepository> conversationRepo_;
    std::unique_ptr<MessageRepository> messageRepo_;
    bool initialized_ = false;
};

}
}
