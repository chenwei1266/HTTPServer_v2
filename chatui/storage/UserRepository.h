#pragma once

#include "User.h"
#include <optional>
#include <memory>

// 前向声明
namespace http { namespace db { class DbConnectionPool; } }

namespace chatui {
namespace storage {

class UserRepository {
public:
    explicit UserRepository(::http::db::DbConnectionPool* pool);
    ~UserRepository() = default;

    std::optional<User> findById(int userId);
    std::optional<User> findByEmail(const std::string& email);
    std::optional<User> findByUsername(const std::string& username);

    int insert(const User& user);  // 返回新插入的 userId
    bool update(const User& user);
    bool deleteById(int userId);

private:
    ::http::db::DbConnectionPool* pool_;
};

} // namespace storage
} // namespace chatui
