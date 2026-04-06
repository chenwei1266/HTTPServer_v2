#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace chatui_v2 {
namespace json {

inline std::string escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

inline std::string extractString(const std::string& json, const std::string& field)
{
    const std::string key = "\"" + field + "\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return "";
    ++pos;

    std::string result;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            ++pos;
            switch (json[pos])
            {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                default: result += json[pos]; break;
            }
        }
        else
        {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

inline int64_t extractInt64(const std::string& json, const std::string& field, int64_t def = 0)
{
    const std::string key = "\"" + field + "\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return def;
    pos += key.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
    if (pos >= json.size()) return def;

    bool neg = false;
    if (json[pos] == '-')
    {
        neg = true;
        ++pos;
    }
    if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) return def;

    int64_t value = 0;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
    {
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    return neg ? -value : value;
}

inline bool extractBool(const std::string& json, const std::string& field, bool def = false)
{
    const std::string key = "\"" + field + "\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return def;
    pos += key.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) ++pos;
    if (pos >= json.size()) return def;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return def;
}

inline size_t findObjectEnd(const std::string& s, size_t start)
{
    int depth = 0;
    bool inStr = false;
    for (size_t i = start; i < s.size(); ++i)
    {
        if (inStr)
        {
            if (s[i] == '\\') { ++i; continue; }
            if (s[i] == '"') inStr = false;
        }
        else
        {
            if (s[i] == '"') inStr = true;
            else if (s[i] == '{') ++depth;
            else if (s[i] == '}') { --depth; if (depth == 0) return i; }
        }
    }
    return s.size() - 1;
}

inline std::vector<std::pair<std::string, std::string>> extractMessages(const std::string& body)
{
    std::vector<std::pair<std::string, std::string>> out;
    auto pos = body.find("\"messages\"");
    if (pos == std::string::npos) return out;

    pos = body.find('[', pos);
    if (pos == std::string::npos) return out;

    ++pos;
    while (pos < body.size())
    {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\n' || body[pos] == '\r' || body[pos] == '\t' || body[pos] == ',')) ++pos;
        if (pos >= body.size() || body[pos] == ']') break;
        if (body[pos] != '{') { ++pos; continue; }

        size_t end = findObjectEnd(body, pos);
        std::string obj = body.substr(pos, end - pos + 1);
        std::string role = extractString(obj, "role");
        std::string content = extractString(obj, "content");
        if (!role.empty()) out.emplace_back(role, content);
        pos = end + 1;
    }

    return out;
}

} // namespace json
} // namespace chatui_v2
