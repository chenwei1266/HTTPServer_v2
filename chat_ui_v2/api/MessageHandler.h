#pragma once

#include <string>

#include "auth/AuthMiddleware.h"
#include "common/JsonLite.h"
#include "dao/ConversationDao.h"
#include "dao/MessageDao.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "router/RouterHandler.h"
#include "session/SessionManager.h"

namespace chatui_v2 {
namespace api {

class MessageHandler : public http::router::RouterHandler
{
public:
    explicit MessageHandler(http::session::SessionManager* sm) : sm_(sm) {}

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

        auto conv = dao::ConversationDao::findById(convId, userId);
        if (conv.id == 0)
        {
            resp->setStatusCode(http::HttpResponse::k404NotFound);
            resp->setBody(R"({"error":"conversation not found"})");
            return;
        }

        auto msgs = dao::MessageDao::listByConversation(convId);
        std::string body = "{\"items\":[";
        for (size_t i = 0; i < msgs.size(); ++i)
        {
            if (i > 0) body += ',';
            body += std::string("{\"id\":") + std::to_string(msgs[i].id)
                + ",\"role\":\"" + json::escape(msgs[i].role)
                + "\",\"content\":\"" + json::escape(msgs[i].content)
                + "\",\"created_at\":" + std::to_string(msgs[i].createdAt)
                + "}";
        }
        body += "]}";

        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setBody(body);
    }

private:
    http::session::SessionManager* sm_;
};

} // namespace api
} // namespace chatui_v2
