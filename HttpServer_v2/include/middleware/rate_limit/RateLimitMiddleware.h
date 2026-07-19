#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "../../http/HttpRequest.h"
#include "../../http/HttpResponse.h"
#include "../Middleware.h"
#include <sw/redis++/redis++.h>

namespace http {
namespace middleware {

struct RateLimitConfig {
    int loginPerMinute = 5;
    int loginPer15Min = 20;

    int ssePerMinute = 30;
    int sseBurst = 10;

    int sseConnLimit = 3;
    int sseConnTtlSec = 3600;
};

class RateLimiter {
public:
    explicit RateLimiter(const std::string& redisUri, const RateLimitConfig& cfg = {})
        : redis_(redisUri), cfg_(cfg) {}

    bool allowLogin(const std::string& clientIp,
                    const std::string& email,
                    int* retryAfterSec = nullptr);

    bool allowSseRequest(const std::string& userKey, int* retryAfterSec = nullptr);

    bool acquireSseConnectionSlot(const std::string& userKey, int* retryAfterSec = nullptr);
    void releaseSseConnectionSlot(const std::string& userKey);

    const RateLimitConfig& config() const { return cfg_; }

private:
    static long long nowMs();
    static std::string hashKey(const std::string& raw);

private:
    sw::redis::Redis redis_;
    RateLimitConfig cfg_;
};

class RateLimitMiddleware : public Middleware {
public:
    explicit RateLimitMiddleware(std::shared_ptr<RateLimiter> limiter)
        : limiter_(std::move(limiter)) {}

    void before(HttpRequest& request) override;
    void after(HttpResponse&) override {}

private:
    static std::string extractClientIp(const HttpRequest& req);
    static std::string extractJsonStringField(const std::string& body, const std::string& field);
    static HttpResponse tooManyRequestsResponse(int retryAfterSec, const std::string& message);

private:
    std::shared_ptr<RateLimiter> limiter_;
};

}  // namespace middleware
}  // namespace http
