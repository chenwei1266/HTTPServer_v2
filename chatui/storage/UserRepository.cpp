#include "UserRepository.h"
#include "../../HttpServer_v2/include/utils/db/DbConnectionPool.h"
#include "../../HttpServer_v2/include/utils/db/DbConnection.h"
#include <iostream>
#include <memory>

using http::db::DbConnectionPool;
using http::db::DbConnection;

namespace chatui {
namespace storage {

UserRepository::UserRepository(DbConnectionPool* pool) : pool_(pool) {
}

std::optional<User> UserRepository::findById(int userId) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "SELECT id, username, email, password_hash, salt, UNIX_TIMESTAMP(created_at) as created_at, UNIX_TIMESTAMP(updated_at) as updated_at FROM users WHERE id = ?";
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery(sql, userId));

        if (rs->next()) {
            User user;
            user.id = rs->getInt("id");
            user.username = rs->getString("username");
            user.email = rs->getString("email");
            user.passwordHash = rs->getString("password_hash");
            user.salt = rs->getString("salt");
            user.createdAt = rs->getInt64("created_at");
            user.updatedAt = rs->getInt64("updated_at");
            
            return user;
        }
        
    } catch (const std::exception& e) {
        
        std::cerr << "[UserRepository] findById error: " << e.what() << "\n";
    }
    return std::nullopt;
}

std::optional<User> UserRepository::findByEmail(const std::string& email) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "SELECT id, username, email, password_hash, salt, UNIX_TIMESTAMP(created_at) as created_at, UNIX_TIMESTAMP(updated_at) as updated_at FROM users WHERE email = ?";
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery(sql, email));

        if (rs->next()) {
            User user;
            user.id = rs->getInt("id");
            user.username = rs->getString("username");
            user.email = rs->getString("email");
            user.passwordHash = rs->getString("password_hash");
            user.salt = rs->getString("salt");
            user.createdAt = rs->getInt64("created_at");
            user.updatedAt = rs->getInt64("updated_at");
            
            return user;
        }
        
    } catch (const std::exception& e) {
        
        std::cerr << "[UserRepository] findByEmail error: " << e.what() << "\n";
    }
    return std::nullopt;
}

std::optional<User> UserRepository::findByUsername(const std::string& username) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "SELECT id, username, email, password_hash, salt, UNIX_TIMESTAMP(created_at) as created_at, UNIX_TIMESTAMP(updated_at) as updated_at FROM users WHERE username = ?";
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery(sql, username));

        if (rs->next()) {
            User user;
            user.id = rs->getInt("id");
            user.username = rs->getString("username");
            user.email = rs->getString("email");
            user.passwordHash = rs->getString("password_hash");
            user.salt = rs->getString("salt");
            user.createdAt = rs->getInt64("created_at");
            user.updatedAt = rs->getInt64("updated_at");
            
            return user;
        }
        
    } catch (const std::exception& e) {
        
        std::cerr << "[UserRepository] findByUsername error: " << e.what() << "\n";
    }
    return std::nullopt;
}

int UserRepository::insert(const User& user) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "INSERT INTO users (username, email, password_hash, salt, created_at, updated_at) VALUES (?, ?, ?, ?, FROM_UNIXTIME(?), FROM_UNIXTIME(?))";
        conn->executeUpdate(sql, user.username, user.email, user.passwordHash, user.salt, user.createdAt, user.updatedAt);

        // 获取插入的 ID
        std::unique_ptr<sql::ResultSet> rs(conn->executeQuery("SELECT LAST_INSERT_ID() as id"));
        int userId = -1;
        if (rs->next()) {
            userId = rs->getInt("id");
        }
        
        return userId;
    } catch (const std::exception& e) {
        
        std::cerr << "[UserRepository] insert error: " << e.what() << "\n";
        return -1;
    }
}

bool UserRepository::update(const User& user) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "UPDATE users SET username = ?, email = ?, updated_at = FROM_UNIXTIME(?) WHERE id = ?";
        int affected = conn->executeUpdate(sql, user.username, user.email, user.updatedAt, user.id);
        
        return affected > 0;
    } catch (const std::exception& e) {
        
        std::cerr << "[UserRepository] update error: " << e.what() << "\n";
        return false;
    }
}

bool UserRepository::deleteById(int userId) {
    auto conn = pool_->getConnection();
    try {
        std::string sql = "DELETE FROM users WHERE id = ?";
        int affected = conn->executeUpdate(sql, userId);
        
        return affected > 0;
    } catch (const std::exception& e) {
        
        std::cerr << "[UserRepository] deleteById error: " << e.what() << "\n";
        return false;
    }
}

} // namespace storage
} // namespace chatui
