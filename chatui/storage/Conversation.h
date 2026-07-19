#pragma once

#include <string>
#include <ctime>

namespace chatui {
namespace storage {

struct Conversation {
    int id = 0;
    int userId = 0;
    std::string title;
    std::string model;
    std::string systemPrompt;
    std::time_t createdAt = 0;
    std::time_t updatedAt = 0;
};

} // namespace storage
} // namespace chatui
