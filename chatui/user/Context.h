#pragma once

#include "../llm/LLMManager.h"
#include <vector>

namespace chatui {
namespace user {

// Context - 对话上下文，包含完整的消息历史
struct Context {
    int conversationId = 0;
    std::string model;
    std::string systemPrompt;
    std::vector<llm::Message> messages;  // 历史消息列表

    void addMessage(const std::string& role, const std::string& content) {
        messages.push_back({role, content});
    }

    void clear() {
        messages.clear();
    }
};

} // namespace user
} // namespace chatui
