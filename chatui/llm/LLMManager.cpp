#include "LLMManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <optional>

namespace chatui {
namespace llm {

// ════════════════════════════════════════════════════════════
//  构造与析构
// ════════════════════════════════════════════════════════════

LLMManager::LLMManager() {
    std::cout << "[LLMManager] Initialized\n";
}

LLMManager::~LLMManager() {
    shutdown(5000);
}

// ════════════════════════════════════════════════════════════
//  配置加载
// ════════════════════════════════════════════════════════════

bool LLMManager::loadConfig(const std::string& configPath) {
    std::ifstream ifs(configPath);
    if (!ifs.is_open()) {
        std::cerr << "[LLMManager] Failed to open config file: " << configPath << "\n";
        return false;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string jsonContent = oss.str();

    // 简单的 JSON 解析（手工实现，适配现有代码风格）
    // 格式：
    // {
    //   "models": [
    //     {
    //       "name": "qwen-plus",
    //       "provider": "qwen",
    //       "model_id": "qwen-plus",
    //       "api_key": "sk-xxx",
    //       "base_url": "https://dashscope.aliyuncs.com",
    //       "max_tokens": 4096,
    //       "timeout": 120,
    //       "enabled": true
    //     }
    //   ]
    // }

    auto extractString = [](const std::string& json, const std::string& key) -> std::string {
        std::string k = "\"" + key + "\"";
        auto pos = json.find(k);
        if (pos == std::string::npos) return "";
        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t'))
            ++pos;
        if (pos >= json.size() || json[pos] != '"') return "";
        ++pos;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case '"': result += '"';  break;
                    case '\\': result += '\\'; break;
                    default: result += json[pos]; break;
                }
            } else {
                result += json[pos];
            }
            ++pos;
        }
        return result;
    };

    auto extractInt = [](const std::string& json, const std::string& key, int def) -> int {
        std::string k = "\"" + key + "\"";
        auto pos = json.find(k);
        if (pos == std::string::npos) return def;
        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
        if (pos >= json.size() || !std::isdigit((unsigned char)json[pos])) return def;
        int val = 0;
        while (pos < json.size() && std::isdigit((unsigned char)json[pos]))
            val = val * 10 + (json[pos++] - '0');
        return val;
    };

    auto extractBool = [](const std::string& json, const std::string& key, bool def) -> bool {
        std::string k = "\"" + key + "\"";
        auto pos = json.find(k);
        if (pos == std::string::npos) return def;
        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
        if (pos >= json.size()) return def;
        if (json.substr(pos, 4) == "true") return true;
        if (json.substr(pos, 5) == "false") return false;
        return def;
    };

    auto findObjectEnd = [](const std::string& s, size_t start) -> size_t {
        int depth = 0;
        bool inStr = false;
        for (size_t i = start; i < s.size(); ++i) {
            if (inStr) {
                if (s[i] == '\\') { ++i; continue; }
                if (s[i] == '"') inStr = false;
            } else {
                if (s[i] == '"') inStr = true;
                else if (s[i] == '{') ++depth;
                else if (s[i] == '}') { --depth; if (!depth) return i; }
            }
        }
        return s.size() - 1;
    };

    // 查找 "models" 数组
    auto modelsPos = jsonContent.find("\"models\"");
    if (modelsPos == std::string::npos) {
        std::cerr << "[LLMManager] No 'models' field found in config\n";
        return false;
    }

    modelsPos = jsonContent.find('[', modelsPos);
    if (modelsPos == std::string::npos) {
        std::cerr << "[LLMManager] Invalid 'models' array format\n";
        return false;
    }

    size_t pos = modelsPos + 1;
    int loadedCount = 0;

    while (pos < jsonContent.size()) {
        // 跳过空白
        while (pos < jsonContent.size() && std::isspace((unsigned char)jsonContent[pos])) ++pos;
        if (pos >= jsonContent.size() || jsonContent[pos] == ']') break;
        if (jsonContent[pos] != '{') { ++pos; continue; }

        // 提取对象
        size_t objEnd = findObjectEnd(jsonContent, pos);
        std::string obj = jsonContent.substr(pos, objEnd - pos + 1);

        ModelInfo info;
        info.name = extractString(obj, "name");
        info.provider = extractString(obj, "provider");
        info.modelId = extractString(obj, "model_id");
        info.apiKey = extractString(obj, "api_key");
        info.baseUrl = extractString(obj, "base_url");
        info.maxTokens = extractInt(obj, "max_tokens", 4096);
        info.timeout = extractInt(obj, "timeout", 120);
        info.enabled = extractBool(obj, "enabled", true);

        // 去除 baseUrl 尾部斜杠
        while (!info.baseUrl.empty() && info.baseUrl.back() == '/')
            info.baseUrl.pop_back();

        if (!info.name.empty() && !info.provider.empty() && info.enabled) {
            std::lock_guard<std::mutex> lock(mutex_);
            models_[info.name] = info;
            ++loadedCount;
            std::cout << "[LLMManager] Loaded model: " << info.name
                      << " (provider: " << info.provider << ")\n";
        }

        pos = objEnd + 1;
    }

    std::cout << "[LLMManager] Loaded " << loadedCount << " model(s) from " << configPath << "\n";
    return loadedCount > 0;
}

std::vector<ModelInfo> LLMManager::getModelList() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelInfo> list;
    list.reserve(models_.size());
    for (const auto& [name, info] : models_) {
        list.push_back(info);
    }
    return list;
}

std::optional<ModelInfo> LLMManager::getModel(const std::string& modelName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = models_.find(modelName);
    if (it != models_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ════════════════════════════════════════════════════════════
//  推理接口
// ════════════════════════════════════════════════════════════

std::string LLMManager::get(
    const RequestContext& context,
    OnEventCallback onEvent,
    OnDoneCallback onDone)
{
    if (shutdown_.load()) {
        InferenceResult result;
        result.success = false;
        result.error = "LLMManager is shutting down";
        onDone(result);
        return "";
    }

    // 检查模型是否存在
    auto modelInfo = getModel(context.model);
    if (!modelInfo.has_value()) {
        InferenceResult result;
        result.success = false;
        result.error = "Model not found: " + context.model;
        onDone(result);
        return "";
    }

    // 生成请求ID
    std::string requestId = generateRequestId();

    // 创建活跃请求
    auto request = std::make_unique<ActiveRequest>();
    request->requestId = requestId;
    request->model = context.model;
    request->startTime = std::chrono::steady_clock::now();

    // 启动工作线程
    request->worker = std::thread([this, requestId, context, onEvent, onDone]() {
        executeInference(requestId, context, onEvent, onDone);
    });

    // 注册到活跃请求列表
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activeRequests_[requestId] = std::move(request);
    }

    std::cout << "[LLMManager] Started inference request: " << requestId
              << " (model: " << context.model << ")\n";

    return requestId;
}

bool LLMManager::cancel(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = activeRequests_.find(requestId);
    if (it != activeRequests_.end()) {
        it->second->cancelled.store(true);
        std::cout << "[LLMManager] Cancelled request: " << requestId << "\n";
        return true;
    }
    return false;
}

size_t LLMManager::getActiveRequestCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeRequests_.size();
}

// ════════════════════════════════════════════════════════════
//  生命周期管理
// ════════════════════════════════════════════════════════════

void LLMManager::shutdown(int timeoutMs) {
    if (shutdown_.exchange(true)) {
        return; // 已经关闭
    }

    std::cout << "[LLMManager] Shutting down...\n";

    auto startTime = std::chrono::steady_clock::now();

    // 等待所有请求完成
    while (true) {
        std::vector<std::thread::id> activeThreads;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeRequests_.empty()) break;

            for (auto& [id, req] : activeRequests_) {
                activeThreads.push_back(req->worker.get_id());
            }
        }

        // 检查超时
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        if (timeoutMs > 0 && elapsed > timeoutMs) {
            std::cerr << "[LLMManager] Shutdown timeout, "
                      << activeThreads.size() << " requests still active\n";
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 等待所有线程结束
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, req] : activeRequests_) {
            if (req->worker.joinable()) {
                threads.push_back(std::move(req->worker));
            }
        }
        activeRequests_.clear();
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::cout << "[LLMManager] Shutdown complete\n";
}

// ════════════════════════════════════════════════════════════
//  内部实现
// ════════════════════════════════════════════════════════════

std::string LLMManager::generateRequestId() {
    uint64_t counter = requestCounter_.fetch_add(1);
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "req_" << std::hex << timestamp << "_" << counter;
    return oss.str();
}

void LLMManager::executeInference(
    const std::string& requestId,
    const RequestContext& context,
    OnEventCallback onEvent,
    OnDoneCallback onDone)
{
    InferenceResult result;
    result.requestId = requestId;
    auto startTime = std::chrono::steady_clock::now();

    try {
        // 获取模型配置
        auto modelInfo = getModel(context.model);
        if (!modelInfo.has_value()) {
            result.success = false;
            result.error = "Model not found: " + context.model;
            onDone(result);
            removeRequest(requestId);
            return;
        }

        // 检查是否已取消
        ActiveRequest* activeReq = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeRequests_.find(requestId);
            if (it != activeRequests_.end()) {
                activeReq = it->second.get();
            }
        }

        if (!activeReq || activeReq->cancelled.load()) {
            result.success = false;
            result.error = "Request cancelled";
            onDone(result);
            removeRequest(requestId);
            return;
        }

        // TODO: 创建策略并执行推理
        // auto strategy = createStrategy(modelInfo.value());
        //
        // std::string fullContent;
        // strategy->sendStreamMsg(
        //     context.messages,
        //     [&](const std::string& token) {
        //         if (activeReq->cancelled.load()) return;
        //         fullContent += token;
        //         StreamEvent event;
        //         event.type = StreamEvent::TOKEN;
        //         event.content = token;
        //         onEvent(event);
        //     },
        //     [&]() {
        //         // 完成
        //         result.success = true;
        //         result.fullContent = fullContent;
        //     },
        //     [&](const std::string& error) {
        //         // 错误
        //         result.success = false;
        //         result.error = error;
        //     }
        // );

        // 临时实现：模拟推理
        std::cout << "[LLMManager] Executing inference for " << requestId << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        result.success = true;
        result.fullContent = "[TODO] Implement strategy execution";

        // 发送完成事件
        StreamEvent doneEvent;
        doneEvent.type = StreamEvent::DONE;
        doneEvent.model = context.model;
        onEvent(doneEvent);

    } catch (const std::exception& e) {
        result.success = false;
        result.error = std::string("Exception: ") + e.what();
    }

    // 计算耗时
    auto endTime = std::chrono::steady_clock::now();
    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    std::cout << "[LLMManager] Inference completed: " << requestId
              << " (duration: " << result.durationMs << "ms)\n";

    onDone(result);
    removeRequest(requestId);
}

void LLMManager::removeRequest(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = activeRequests_.find(requestId);
    if (it != activeRequests_.end()) {
        if (it->second->worker.joinable()) {
            it->second->worker.detach();
        }
        activeRequests_.erase(it);
    }
}

// TODO: 实现策略创建，需要先实现 AIStrategy 层
// std::unique_ptr<AIStrategy> LLMManager::createStrategy(const ModelInfo& modelInfo) {
//     return nullptr;
// }

} // namespace llm
} // namespace chatui
