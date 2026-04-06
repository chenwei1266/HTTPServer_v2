#include "AnthropicClient.h"
#include <curl/curl.h>
#include <functional>
#include <string>

// Parse "data: {...}" lines and extract choices[0].delta.content (minimal, no full JSON lib needed)
static std::string extractToken(const std::string& line)
{
    // line: "data: {....}"
    auto pos = line.find("\"content\":\"");
    if (pos == std::string::npos) return "";
    pos += 11;
    std::string result;
    for (; pos < line.size(); ++pos)
    {
        if (line[pos] == '"' && (pos == 0 || line[pos-1] != '\\')) break;
        if (line[pos] == '\\' && pos + 1 < line.size())
        {
            char next = line[pos+1];
            if (next == 'n') { result += '\n'; ++pos; }
            else if (next == '"') { result += '"'; ++pos; }
            else if (next == '\\') { result += '\\'; ++pos; }
            else result += line[pos];
        }
        else result += line[pos];
    }
    return result;
}

struct CurlCtx {
    std::string buf;
    std::function<void(const std::string&)> onToken;
};

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* ctx = static_cast<CurlCtx*>(userdata);
    ctx->buf.append(ptr, size * nmemb);
    // process complete lines
    size_t pos;
    while ((pos = ctx->buf.find('\n')) != std::string::npos)
    {
        std::string line = ctx->buf.substr(0, pos);
        ctx->buf.erase(0, pos + 1);
        if (line.empty() || line == "\r") continue;
        if (line.rfind("data: ", 0) == 0)
        {
            std::string data = line.substr(6);
            if (data == "[DONE]" || data == "[DONE]\r") return size * nmemb;
            // skip first empty-content delta
            auto token = extractToken(data);
            if (!token.empty()) ctx->onToken(token);
        }
    }
    return size * nmemb;
}

bool streamChat(const std::string& apiUrl,
                const std::string& userMessage,
                std::function<void(const std::string& token)> onToken)
{
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // JSON-escape the user message
    std::string escaped;
    for (char c : userMessage) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else escaped += c;
    }

    std::string body = R"({"messages":[{"role":"user","content":")"
                       + escaped + R"("}],"stream":true,"max_tokens":1024})";

    CurlCtx ctx;
    ctx.onToken = std::move(onToken);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}
