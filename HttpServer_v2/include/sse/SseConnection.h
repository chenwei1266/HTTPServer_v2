#pragma once

#include <memory>
#include <string>
#include <muduo/net/TcpConnection.h>

namespace sse
{

class SseConnection;
using SseConnectionPtr = std::shared_ptr<SseConnection>;

class SseConnection
{
public:
    explicit SseConnection(const muduo::net::TcpConnectionPtr& conn);
    void send(const std::string& data, const std::string& event = "");
    void close();
    bool connected() const;

private:
    muduo::net::TcpConnectionPtr conn_;
};

} // namespace sse
