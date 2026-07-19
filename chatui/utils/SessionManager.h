#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

namespace chatui {
namespace utils {

// 简单的 Session 管理器
class SessionManager {
public:
    static SessionManager& getInstance() {
        static SessionManager instance;
        return instance;
    }

    // 创建 session，返回 token
    std::string createSession(int userId) {
        std::string token = generateToken();
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[token] = SessionData{userId, std::time(nullptr)};
        return token;
    }

    // 验证 token，返回 userId
    int validateSession(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) {
            return -1;
        }

        // 检查是否过期（24小时）
        std::time_t now = std::time(nullptr);
        if (now - it->second.createdAt > 86400) {
            sessions_.erase(it);
            return -1;
        }

        return it->second.userId;
    }

    // 删除 session
    void removeSession(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(token);
    }

    // 清理过期 session
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::time_t now = std::time(nullptr);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (now - it->second.createdAt > 86400) {
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    SessionManager() = default;

    struct SessionData {
        int userId;
        std::time_t createdAt;
    };

    std::string generateToken() {
        static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

        std::string token;
        token.reserve(32);
        for (int i = 0; i < 32; ++i) {
            token += charset[dis(gen)];
        }
        return token;
    }

    std::mutex mutex_;
    std::unordered_map<std::string, SessionData> sessions_;
};

} // namespace utils
} // namespace chatui
