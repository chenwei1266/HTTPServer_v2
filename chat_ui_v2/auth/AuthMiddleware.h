#pragma once

#include <cstdint>
#include <string>

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "session/SessionManager.h"

namespace chatui_v2 {
namespace auth {

class AuthMiddleware
{
public:
    static bool check(const http::HttpRequest& req,
                      http::HttpResponse* resp,
                      http::session::SessionManager* sm,
                      int64_t& outUserId)
    {
        outUserId = 0;
        if (!sm)
        {
            resp->setStatusCode(http::HttpResponse::k500InternalServerError);
            resp->setContentType("application/json; charset=utf-8");
            resp->setBody(R"({"error":"session manager missing"})");
            return false;
        }

        auto session = sm->getSession(req, resp);
        std::string uid = session->getValue("user_id");
        if (uid.empty())
        {
            resp->setStatusCode(http::HttpResponse::k401Unauthorized);
            resp->setContentType("application/json; charset=utf-8");
            resp->setBody(R"({"error":"not logged in"})");
            return false;
        }

        try { outUserId = std::stoll(uid); }
        catch (...) { outUserId = 0; }

        if (outUserId <= 0)
        {
            resp->setStatusCode(http::HttpResponse::k401Unauthorized);
            resp->setContentType("application/json; charset=utf-8");
            resp->setBody(R"({"error":"invalid session"})");
            return false;
        }

        return true;
    }
};

} // namespace auth
} // namespace chatui_v2
