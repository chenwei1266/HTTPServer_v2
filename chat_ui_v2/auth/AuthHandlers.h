#pragma once

#include <regex>
#include <string>

#include "common/JsonLite.h"
#include "dao/UserDao.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "router/RouterHandler.h"
#include "session/SessionManager.h"

namespace chatui_v2 {
namespace auth {

inline bool isLikelyEmail(const std::string& email)
{
    static const std::regex kEmail(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
    return std::regex_match(email, kEmail);
}

inline std::string userJson(const dao::User& u)
{
    return std::string("{\"id\":") + std::to_string(u.id)
        + ",\"username\":\"" + json::escape(u.username)
        + "\",\"name\":\"" + json::escape(u.username)
        + "\",\"email\":\"" + json::escape(u.email) + "\"}";
}

class RegisterHandler : public http::router::RouterHandler
{
public:
    explicit RegisterHandler(http::session::SessionManager* sm) : sm_(sm) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override
    {
        const std::string body = req.getBody();
        const std::string username = json::extractString(body, "username");
        const std::string email = json::extractString(body, "email");
        const std::string password = json::extractString(body, "password");

        resp->setContentType("application/json; charset=utf-8");
        if (username.size() < 3 || username.size() > 64)
        {
            resp->setStatusCode(http::HttpResponse::k400BadRequest);
            resp->setBody(R"({"error":"username length must be 3-64"})");
            return;
        }
        if (!isLikelyEmail(email))
        {
            resp->setStatusCode(http::HttpResponse::k400BadRequest);
            resp->setBody(R"({"error":"invalid email"})");
            return;
        }
        if (password.size() < 8)
        {
            resp->setStatusCode(http::HttpResponse::k400BadRequest);
            resp->setBody(R"({"error":"password length must be >= 8"})");
            return;
        }

        int64_t userId = dao::UserDao::registerUser(username, email, password);
        if (userId == -1)
        {
            resp->setStatusCode(http::HttpResponse::k409Conflict);
            resp->setBody(R"({"error":"email already exists"})");
            return;
        }
        if (userId == 0)
        {
            resp->setStatusCode(http::HttpResponse::k500InternalServerError);
            resp->setBody(R"({"error":"register failed"})");
            return;
        }

        dao::User user = dao::UserDao::findById(userId);
        if (sm_)
        {
            auto session = sm_->getSession(req, resp);
            session->setValue("user_id", std::to_string(user.id));
            session->setValue("username", user.username);
            session->setValue("email", user.email);
            sm_->updateSession(session);
        }

        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setBody(std::string("{\"ok\":true,\"access_token\":\"\",\"user\":") + userJson(user) + "}");
    }

private:
    http::session::SessionManager* sm_;
};

class LoginHandler : public http::router::RouterHandler
{
public:
    explicit LoginHandler(http::session::SessionManager* sm) : sm_(sm) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override
    {
        const std::string body = req.getBody();
        const std::string email = json::extractString(body, "email");
        const std::string password = json::extractString(body, "password");

        resp->setContentType("application/json; charset=utf-8");
        if (!isLikelyEmail(email) || password.empty())
        {
            resp->setStatusCode(http::HttpResponse::k400BadRequest);
            resp->setBody(R"({"error":"email and password required"})");
            return;
        }

        dao::User user = dao::UserDao::loginByEmail(email, password);
        if (user.id == 0)
        {
            resp->setStatusCode(http::HttpResponse::k401Unauthorized);
            resp->setBody(R"({"error":"invalid email or password"})");
            return;
        }

        if (sm_)
        {
            auto session = sm_->getSession(req, resp);
            session->setValue("user_id", std::to_string(user.id));
            session->setValue("username", user.username);
            session->setValue("email", user.email);
            sm_->updateSession(session);
        }

        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setBody(std::string("{\"ok\":true,\"access_token\":\"\",\"user\":") + userJson(user) + "}");
    }

private:
    http::session::SessionManager* sm_;
};

class LogoutHandler : public http::router::RouterHandler
{
public:
    explicit LogoutHandler(http::session::SessionManager* sm) : sm_(sm) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override
    {
        resp->setContentType("application/json; charset=utf-8");
        if (sm_)
        {
            auto session = sm_->getSession(req, resp);
            sm_->destroySession(session->getId());
        }
        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setBody(R"({"ok":true})");
    }

private:
    http::session::SessionManager* sm_;
};

} // namespace auth
} // namespace chatui_v2
