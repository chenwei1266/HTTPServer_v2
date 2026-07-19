#include "../../../include/middleware/rate_limit/RateLimitMiddleware.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <random>
#include <sstream>

namespace http {
namespace middleware {

namespace {

const char* kSlidingWindowScript = R"(
local key = KEYS[1]
local now_ms = tonumber(ARGV[1])
local window_ms = tonumber(ARGV[2])
local limit = tonumber(ARGV[3])
local member = ARGV[4]

redis.call('ZREMRANGEBYSCORE', key, 0, now_ms - window_ms)
local cnt = redis.call('ZCARD', key)
if cnt >= limit then
  local oldest = redis.call('ZRANGE', key, 0, 0, 'WITHSCORES')
  local retry = 1
  if oldest and #oldest >= 2 then
    retry = math.ceil((tonumber(oldest[2]) + window_ms - now_ms) / 1000)
    if retry < 1 then retry = 1 end
  end
  return -retry
end
redis.call('ZADD', key, now_ms, member)
redis.call('EXPIRE', key, math.floor(window_ms / 1000) + 2)
return 1
)";

const char* kTokenBucketScript = R"(
local key = KEYS[1]
local rate = tonumber(ARGV[1])
local cap = tonumber(ARGV[2])
local now_ms = tonumber(ARGV[3])
local req = tonumber(ARGV[4])

local data = redis.call('HMGET', key, 'tokens', 'ts')
local tokens = tonumber(data[1])
local ts = tonumber(data[2])
if tokens == nil then tokens = cap end
if ts == nil then ts = now_ms end

local delta = (now_ms - ts) / 1000.0 * rate
if delta < 0 then delta = 0 end
tokens = math.min(cap, tokens + delta)

if tokens >= req then
  tokens = tokens - req
  redis.call('HMSET', key, 'tokens', tokens, 'ts', now_ms)
  local ttl = math.ceil(cap / rate) * 2
  if ttl < 2 then ttl = 2 end
  redis.call('EXPIRE', key, ttl)
  return 1
end

redis.call('HMSET', key, 'tokens', tokens, 'ts', now_ms)
local ttl = math.ceil(cap / rate) * 2
if ttl < 2 then ttl = 2 end
redis.call('EXPIRE', key, ttl)

local retry = math.ceil((req - tokens) / rate)
if retry < 1 then retry = 1 end
return -retry
)";

const char* kAcquireConnScript = R"(
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local ttl = tonumber(ARGV[2])

local cur = tonumber(redis.call('GET', key) or '0')
if cur >= limit then
  return 0
end
cur = redis.call('INCR', key)
if redis.call('TTL', key) < 0 then
  redis.call('EXPIRE', key, ttl)
end
return cur
)";

const char* kReleaseConnScript = R"(
local key = KEYS[1]
if redis.call('EXISTS', key) == 0 then
  return 0
end
local cur = redis.call('DECR', key)
if cur <= 0 then
  redis.call('DEL', key)
  return 0
end
return cur
)";

std::string makeMember(long long now_ms) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream oss;
    oss << now_ms << ":" << dist(rng);
    return oss.str();
}

}  // namespace

long long RateLimiter::nowMs() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string RateLimiter::hashKey(const std::string& raw) {
    return std::to_string(std::hash<std::string>{}(raw));
}

bool RateLimiter::allowLogin(const std::string& clientIp,
                             const std::string& email,
                             int* retryAfterSec) {
    const auto now_ms = nowMs();
    const std::string dim = hashKey(clientIp + "|" + email);

    int retry = 0;

    {
        const std::string key = "rl:login:m1:" + dim;
        const long long r = redis_.eval<long long>(
            kSlidingWindowScript,
            {key},
            {std::to_string(now_ms), std::to_string(60 * 1000), std::to_string(cfg_.loginPerMinute), makeMember(now_ms)});
        if (r <= 0) retry = std::max(retry, static_cast<int>(-r));
    }

    {
        const std::string key = "rl:login:m15:" + dim;
        const long long r = redis_.eval<long long>(
            kSlidingWindowScript,
            {key},
            {std::to_string(now_ms), std::to_string(15 * 60 * 1000), std::to_string(cfg_.loginPer15Min), makeMember(now_ms)});
        if (r <= 0) retry = std::max(retry, static_cast<int>(-r));
    }

    if (retryAfterSec) *retryAfterSec = retry;
    return retry == 0;
}

bool RateLimiter::allowSseRequest(const std::string& userKey, int* retryAfterSec) {
    const auto now_ms = nowMs();
    const std::string key = "rl:sse:tb:" + hashKey(userKey);
    const double rate = static_cast<double>(cfg_.ssePerMinute) / 60.0;
    const long long r = redis_.eval<long long>(
        kTokenBucketScript,
        {key},
        {std::to_string(rate), std::to_string(cfg_.sseBurst), std::to_string(now_ms), "1"});
    if (r > 0) {
        if (retryAfterSec) *retryAfterSec = 0;
        return true;
    }
    if (retryAfterSec) *retryAfterSec = static_cast<int>(-r);
    return false;
}

bool RateLimiter::acquireSseConnectionSlot(const std::string& userKey, int* retryAfterSec) {
    const std::string key = "rl:sse:conn:" + hashKey(userKey);
    const long long r = redis_.eval<long long>(
        kAcquireConnScript,
        {key},
        {std::to_string(cfg_.sseConnLimit), std::to_string(cfg_.sseConnTtlSec)});
    if (r > 0) {
        if (retryAfterSec) *retryAfterSec = 0;
        return true;
    }
    if (retryAfterSec) *retryAfterSec = 1;
    return false;
}

void RateLimiter::releaseSseConnectionSlot(const std::string& userKey) {
    const std::string key = "rl:sse:conn:" + hashKey(userKey);
    redis_.eval<long long>(kReleaseConnScript, {key}, {});
}

void RateLimitMiddleware::before(HttpRequest& request) {
    if (!limiter_) return;

    const auto method = request.method();
    const std::string path = request.path();

    if (method == HttpRequest::kPost && path == "/api/auth/login") {
        const std::string ip = extractClientIp(request);
        const std::string email = extractJsonStringField(request.getBody(), "email");
        int retryAfter = 0;
        if (!limiter_->allowLogin(ip, email, &retryAfter)) {
            throw tooManyRequestsResponse(retryAfter, "login rate limit exceeded");
        }
        return;
    }

    if (method == HttpRequest::kPost && path == "/api/chat/sse") {
        const std::string sid = request.getHeader("Cookie");
        const std::string ip = extractClientIp(request);
        const std::string userKey = sid.empty() ? ip : sid;
        int retryAfter = 0;
        if (!limiter_->allowSseRequest(userKey, &retryAfter)) {
            throw tooManyRequestsResponse(retryAfter, "chat rate limit exceeded");
        }
        return;
    }
}

std::string RateLimitMiddleware::extractClientIp(const HttpRequest& req) {
    auto xff = req.getHeader("X-Forwarded-For");
    if (!xff.empty()) {
        auto pos = xff.find(',');
        if (pos != std::string::npos) xff = xff.substr(0, pos);
        xff.erase(xff.begin(), std::find_if(xff.begin(), xff.end(), [](unsigned char c) { return !std::isspace(c); }));
        xff.erase(std::find_if(xff.rbegin(), xff.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), xff.end());
        if (!xff.empty()) return xff;
    }
    const auto xrip = req.getHeader("X-Real-IP");
    if (!xrip.empty()) return xrip;
    return "unknown";
}

std::string RateLimitMiddleware::extractJsonStringField(const std::string& body, const std::string& field) {
    const std::string key = "\"" + field + "\"";
    auto pos = body.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == ':' || body[pos] == '\n' || body[pos] == '\r' || body[pos] == '\t')) ++pos;
    if (pos >= body.size() || body[pos] != '"') return "";
    ++pos;
    std::string out;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) ++pos;
        out.push_back(body[pos]);
        ++pos;
    }
    return out;
}

HttpResponse RateLimitMiddleware::tooManyRequestsResponse(int retryAfterSec, const std::string& message) {
    HttpResponse resp(false);
    resp.setStatusCode(HttpResponse::kUnknown);
    resp.setStatusLine("HTTP/1.1", static_cast<HttpResponse::HttpStatusCode>(429), "Too Many Requests");
    resp.setContentType("application/json; charset=utf-8");
    resp.addHeader("Retry-After", std::to_string(std::max(1, retryAfterSec)));
    resp.setBody(std::string("{\"error\":\"") + message + "\",\"retry_after\":" + std::to_string(std::max(1, retryAfterSec)) + "}");
    return resp;
}

}  // namespace middleware
}  // namespace http
