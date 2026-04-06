#pragma once

#include <memory>
#include <string>

#include "auth/PasswordUtil.h"
#include "utils/db/DbConnectionPool.h"

namespace chatui_v2 {
namespace dao {

struct User
{
    int64_t id = 0;
    std::string username;
    std::string email;
    std::string passwordHash;
    std::string salt;
};

class UserDao
{
public:
    static User findByEmail(const std::string& email)
    {
        User u;
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        std::unique_ptr<sql::ResultSet> rs(
            conn->executeQuery(
                "SELECT id, username, email, password_hash, salt FROM users WHERE email = ?",
                email));
        if (rs && rs->next())
        {
            u.id = rs->getInt64("id");
            u.username = rs->getString("username");
            u.email = rs->getString("email");
            u.passwordHash = rs->getString("password_hash");
            u.salt = rs->getString("salt");
        }
        return u;
    }

    static User findById(int64_t id)
    {
        User u;
        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        std::unique_ptr<sql::ResultSet> rs(
            conn->executeQuery(
                "SELECT id, username, email, password_hash, salt FROM users WHERE id = ?",
                id));
        if (rs && rs->next())
        {
            u.id = rs->getInt64("id");
            u.username = rs->getString("username");
            u.email = rs->getString("email");
            u.passwordHash = rs->getString("password_hash");
            u.salt = rs->getString("salt");
        }
        return u;
    }

    static int64_t registerUser(const std::string& username,
                                const std::string& email,
                                const std::string& password)
    {
        if (findByEmail(email).id != 0) return -1;

        auto conn = http::db::DbConnectionPool::getInstance().getConnection();
        const std::string salt = ::auth::PasswordUtil::generateSalt();
        const std::string hash = ::auth::PasswordUtil::hashPassword(password, salt);

        conn->executeUpdate(
            "INSERT INTO users (username, email, password_hash, salt) VALUES (?, ?, ?, ?)",
            username, email, hash, salt);

        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery("SELECT LAST_INSERT_ID() AS id"));
        if (rs && rs->next()) return rs->getInt64("id");
        return 0;
    }

    static User loginByEmail(const std::string& email, const std::string& password)
    {
        User user = findByEmail(email);
        if (user.id == 0) return user;
        if (!::auth::PasswordUtil::verify(password, user.salt, user.passwordHash))
        {
            user.id = 0;
        }
        return user;
    }
};

} // namespace dao
} // namespace chatui_v2
