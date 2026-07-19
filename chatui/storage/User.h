#pragma once

#include <string>
#include <ctime>

namespace chatui {
namespace storage {

struct User {
    int id = 0;
    std::string username;
    std::string email;
    std::string passwordHash;
    std::string salt;
    std::time_t createdAt = 0;
    std::time_t updatedAt = 0;
};

} // namespace storage
} // namespace chatui
