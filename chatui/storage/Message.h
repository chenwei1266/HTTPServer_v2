#pragma once

#include <string>
#include <ctime>

namespace chatui {
namespace storage {

struct Message {
    int id = 0;
    int conversationId = 0;
    std::string role;     // "system" | "user" | "assistant"
    std::string content;
    std::time_t createdAt = 0;
};

} // namespace storage
} // namespace chatui
