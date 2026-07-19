#pragma once

#include <string>
#include <sstream>
#include <map>

namespace chatui {
namespace utils {

// 简单的 JSON 解析工具（轻量级实现）
class JsonParser {
public:
    static std::string getString(const std::string& json, const std::string& key, const std::string& defaultValue = "") {
        std::string k = "\"" + key + "\"";
        auto pos = json.find(k);
        if (pos == std::string::npos) return defaultValue;

        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\n'))
            ++pos;

        if (pos >= json.size() || json[pos] != '"') return defaultValue;
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
    }

    static int getInt(const std::string& json, const std::string& key, int defaultValue = 0) {
        std::string k = "\"" + key + "\"";
        auto pos = json.find(k);
        if (pos == std::string::npos) return defaultValue;

        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
        if (pos >= json.size() || !std::isdigit((unsigned char)json[pos])) return defaultValue;

        int val = 0;
        bool negative = false;
        if (json[pos] == '-') {
            negative = true;
            ++pos;
        }
        while (pos < json.size() && std::isdigit((unsigned char)json[pos]))
            val = val * 10 + (json[pos++] - '0');

        return negative ? -val : val;
    }

    static std::string escape(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
};

} // namespace utils
} // namespace chatui
