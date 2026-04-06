#pragma once
#include <functional>
#include <string>

// Streams OpenAI-compatible chat completions.
// onToken is called for each content token; returns false on error.
bool streamChat(const std::string& apiUrl,
                const std::string& userMessage,
                std::function<void(const std::string& token)> onToken);
