#pragma once 

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <muduo/base/ThreadPool.h>

#include "HttpContext.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "../router/Router.h"
#include "../session/SessionManager.h"
#include "../middleware/MiddlewareChain.h"
#include "../middleware/cors/CorsMiddleware.h"
#include "../ssl/SslConnection.h"
#include "../ssl/SslContext.h"
#include "../sse/SseConnection.h"
#include "../ws/WsConnection.h"

class HttpRequest;
class HttpResponse;

namespace http
{

class HttpServer : muduo::noncopyable
{
public:
    using HttpCallback = std::function<void (const http::HttpRequest&, http::HttpResponse*)>;
    
    // 构造函数
    HttpServer(int port,
               const std::string& name,
               bool useSSL = false,
               muduo::net::TcpServer::Option option = muduo::net::TcpServer::kNoReusePort);
    
    void setThreadNum(int numThreads)
    {
        server_.setThreadNum(numThreads);
    }

    // Set worker thread count for SSE/WS handlers (must be called before start())
    void setWorkerThreads(int n)
    {
        workerPool_.setMaxQueueSize(0);
        workerPool_.start(n);
        workerThreads_ = n;
    }

    void start();

    muduo::net::EventLoop* getLoop() const 
    { 
        return server_.getLoop(); 
    }

    void setHttpCallback(const HttpCallback& cb)
    {
        httpCallback_ = cb;
    }

    // 注册静态路由处理器
    void Get(const std::string& path, const HttpCallback& cb)
    {
        router_.registerCallback(HttpRequest::kGet, path, cb);
    }
    
    // 注册静态路由处理器
    void Get(const std::string& path, router::Router::HandlerPtr handler)
    {
        router_.registerHandler(HttpRequest::kGet, path, handler);
    }

    void Post(const std::string& path, const HttpCallback& cb)
    {
        router_.registerCallback(HttpRequest::kPost, path, cb);
    }

    void Post(const std::string& path, router::Router::HandlerPtr handler)
    {
        router_.registerHandler(HttpRequest::kPost, path, handler);
    }

    // 注册动态路由处理器
    void addRoute(HttpRequest::Method method, const std::string& path, router::Router::HandlerPtr handler)
    {
        router_.addRegexHandler(method, path, handler);
    }

    // 注册动态路由处理函数
    void addRoute(HttpRequest::Method method, const std::string& path, const router::Router::HandlerCallback& callback)
    {
        router_.addRegexCallback(method, path, callback);
    }

    // 设置会话管理器
    void setSessionManager(std::unique_ptr<session::SessionManager> manager)
    {
        sessionManager_ = std::move(manager);
    }

    // 获取会话管理器
    session::SessionManager* getSessionManager() const
    {
        return sessionManager_.get();
    }

    // 添加中间件的方法
    void addMiddleware(std::shared_ptr<middleware::Middleware> middleware) 
    {
        middlewareChain_.addMiddleware(middleware);
    }

    void enableSSL(bool enable)
    {
        useSSL_ = enable;
    }

    void setSslConfig(const ssl::SslConfig& config);

    // SSE route
    using SseHandler = std::function<void(const HttpRequest&, const sse::SseConnectionPtr&)>;
    void Sse(const std::string& path, SseHandler handler)
    {
        sseRoutes_[path] = std::move(handler);
    }

    // WebSocket route
    void Ws(const std::string& path,
            std::function<void(const ws::WsConnectionPtr&)> onOpen,
            std::function<void(const ws::WsConnectionPtr&, const std::string&, ws::Opcode)> onMessage,
            std::function<void(const ws::WsConnectionPtr&)> onClose)
    {
        wsRoutes_[path] = ws::WsHandlers{std::move(onOpen), std::move(onMessage), std::move(onClose)};
    }

private:
    void initialize();

    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp receiveTime);
    void onRequest(const muduo::net::TcpConnectionPtr&, const HttpRequest&);

    void handleRequest(const HttpRequest& req, HttpResponse* resp);
    void handleWsUpgrade(const muduo::net::TcpConnectionPtr& conn,
                         const HttpRequest& req,
                         const ws::WsHandlers& handlers);
    void dispatch(std::function<void()> task)
    {
        if (workerThreads_ > 0)
            workerPool_.run(std::move(task));
        else
            task();
    }

private:
    muduo::net::InetAddress                      listenAddr_;
    muduo::net::TcpServer                        server_;
    muduo::net::EventLoop                        mainLoop_;
    muduo::ThreadPool                            workerPool_;
    int                                          workerThreads_ { 0 };
    HttpCallback                                 httpCallback_;
    router::Router                               router_;
    std::unique_ptr<session::SessionManager>     sessionManager_;
    middleware::MiddlewareChain                  middlewareChain_;
    std::unique_ptr<ssl::SslContext>             sslCtx_;
    bool                                         useSSL_;
    std::map<muduo::net::TcpConnectionPtr, std::unique_ptr<ssl::SslConnection>> sslConns_;
    std::unordered_map<std::string, SseHandler>  sseRoutes_;
    std::unordered_map<std::string, ws::WsHandlers> wsRoutes_;
}; 

} // namespace http