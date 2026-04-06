#pragma once

#include <string>

#include "auth/AuthMiddleware.h"
#include "common/JsonLite.h"
#include "dao/ConversationDao.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "router/RouterHandler.h"
#include "session/SessionManager.h"

namespace chatui_v2 {
namespace api {

class ConversationListHandler : public http::router::RouterHandler
{
public:
    explicit ConversationListHandler(http::session::SessionManager* sm) : sm_(sm) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override
    {
        resp->setContentType("application/json; charset=utf-8");
        int64_t userId = 0;
        if (!auth::AuthMiddleware::check(req, resp, sm_, userId)) return;

        if (req.method() == http::HttpRequest::kGet)
        {
            auto list = dao::ConversationDao::listByUser(userId);
            std::string body = "{\"items\":[";
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (i > 0) body += ',';
                body += std::string("{\"id\":") + std::to_string(list[i].id)
                    + ",\"title\":\"" + json::escape(list[i].title)
                    + "\",\"model\":\"" + json::escape(list[i].model)
                    + "\",\"created_at\":" + std::to_string(list[i].createdAt)
                    + ",\"updated_at\":" + std::to_string(list[i].updatedAt)
                    + "}";
            }
            body += "]}";
            resp->setStatusCode(http::HttpResponse::k200Ok);
            resp->setBody(body);
            return;
        }

        if (req.method() == http::HttpRequest::kPost)
        {
            const std::string body = req.getBody();
            std::string title = json::extractString(body, "title");
            std::string model = json::extractString(body, "model");
            std::string systemPrompt = json::extractString(body, "system_prompt");
            if (title.empty()) title = "新对话";
            if (model.empty()) model = "";

            const int64_t id = dao::ConversationDao::create(userId, title, model, systemPrompt);
            auto c = dao::ConversationDao::findById(id, userId);

            resp->setStatusCode(http::HttpResponse::k200Ok);
            resp->setBody(
                std::string("{\"conversation\":{\"id\":") + std::to_string(c.id)
                + ",\"title\":\"" + json::escape(c.title)
                + "\",\"model\":\"" + json::escape(c.model)
                + "\",\"created_at\":" + std::to_string(c.createdAt)
                + ",\"updated_at\":" + std::to_string(c.updatedAt)
                + "}}"
            );
            return;
        }

        resp->setStatusCode(http::HttpResponse::k400BadRequest);
        resp->setBody(R"({"error":"method not allowed"})");
    }

private:
    http::session::SessionManager* sm_;
};

class ConversationDetailHandler : public http::router::RouterHandler
{
public:
    explicit ConversationDetailHandler(http::session::SessionManager* sm) : sm_(sm) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override
    {
        resp->setContentType("application/json; charset=utf-8");
        int64_t userId = 0;
        if (!auth::AuthMiddleware::check(req, resp, sm_, userId)) return;

        std::string idStr = req.getPathParameters("param1");
        if (idStr.empty())
        {
            resp->setStatusCode(http::HttpResponse::k400BadRequest);
            resp->setBody(R"({"error":"missing conversation id"})");
            return;
        }

        int64_t convId = 0;
        try { convId = std::stoll(idStr); }
        catch (...) { convId = 0; }

        if (convId <= 0)
        {
            resp->setStatusCode(http::HttpResponse::k400BadRequest);
            resp->setBody(R"({"error":"invalid conversation id"})");
            return;
        }

        if (req.method() == http::HttpRequest::kPatch || req.method() == http::HttpRequest::kPut)
        {
            std::string title = json::extractString(req.getBody(), "title");
            if (title.empty())
            {
                resp->setStatusCode(http::HttpResponse::k400BadRequest);
                resp->setBody(R"({"error":"title required"})");
                return;
            }
            bool ok = dao::ConversationDao::updateTitle(convId, userId, title);
            resp->setStatusCode(http::HttpResponse::k200Ok);
            resp->setBody(ok ? R"({"ok":true})" : R"({"ok":false,"error":"not found"})");
            return;
        }

        if (req.method() == http::HttpRequest::kDelete)
        {
            bool ok = dao::ConversationDao::remove(convId, userId);
            resp->setStatusCode(http::HttpResponse::k200Ok);
            resp->setBody(ok ? R"({"ok":true})" : R"({"ok":false,"error":"not found"})");
            return;
        }

        resp->setStatusCode(http::HttpResponse::k400BadRequest);
        resp->setBody(R"({"error":"method not allowed"})");
    }

private:
    http::session::SessionManager* sm_;
};

} // namespace api
} // namespace chatui_v2
