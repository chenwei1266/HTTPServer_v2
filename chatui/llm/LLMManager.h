#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <optional>

namespace chatui {
namespace llm {

struct Message {
    std::string role;     // "system" | "user" | "assistant"
    std::string content;
};

struct StreamEvent {
    enum Type { TOKEN, META, ERROR, DONE };
    Type type;
    std::string content;
    std::string model;
    int inputTokens = 0;
    int outputTokens = 0;
};

struct RequestContext {
    std::string userId;
    std::string conversationId;
    std::string model;
    std::vector<Message> messages;
    int maxTokens = 4096;
    float temperature = 0.7f;
    std::string systemPrompt;
};

struct InferenceResult {
    bool success = false;
    std::string requestId;
    std::string fullContent;
    std::string error;
    int inputTokens = 0;
    int outputTokens = 0;
    long durationMs = 0;
};

struct ModelInfo {
    std::string name;       // 模型显示名称
    std::string provider;   // 厂商: qwen/claude/doubao/wenxin/local
    std::string modelId;    // 实际模型ID
    std::string apiKey;
    std::string baseUrl;
    int maxTokens = 4096;
    int timeout = 120;
    bool enabled = true;
};

using OnEventCallback = std::function<void(const StreamEvent&)>;
using OnDoneCallback  = std::function<void(const InferenceResult&)>;

// LLMManager - 管理模型配置与异步推理请求
class LLMManager {
public:
    LLMManager();
    ~LLMManager();

    LLMManager(const LLMManager&) = delete;
    LLMManager& operator=(const LLMManager&) = delete;

    bool loadConfig(const std::string& configPath);  // 加载 models.json
    std::vector<ModelInfo> getModelList() const;
    std::optional<ModelInfo> getModel(const std::string& modelName) const;

    std::string get(const RequestContext& context, OnEventCallback onEvent, OnDoneCallback onDone);  // 异步推理，返回 requestId
    bool cancel(const std::string& requestId);
    size_t getActiveRequestCount() const;

    void shutdown(int timeoutMs = 5000);  // 优雅关闭

private:
    struct ActiveRequest {
        std::string requestId;
        std::string model;
        std::atomic<bool> cancelled{false};
        std::thread worker;
        std::chrono::steady_clock::time_point startTime;
    };

    std::string generateRequestId();
    void executeInference(const std::string& requestId, const RequestContext& context, OnEventCallback onEvent, OnDoneCallback onDone);
    void removeRequest(const std::string& requestId);
    // std::unique_ptr<class AIStrategy> createStrategy(const ModelInfo& modelInfo);  // TODO: 实现策略层后启用

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ModelInfo> models_;
    std::unordered_map<std::string, std::unique_ptr<ActiveRequest>> activeRequests_;
    std::atomic<uint64_t> requestCounter_{0};
    std::atomic<bool> shutdown_{false};
};

} // namespace llm
} // namespace chatui
