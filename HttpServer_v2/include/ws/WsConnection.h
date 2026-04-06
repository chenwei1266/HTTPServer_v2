#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include "WsFrame.h"

namespace ws
{

class WsConnection;
using WsConnectionPtr = std::shared_ptr<WsConnection>;

class WsConnection : public std::enable_shared_from_this<WsConnection>
{
public:
    using MessageCb = std::function<void(const WsConnectionPtr&, const std::string&, Opcode)>;
    using CloseCb   = std::function<void(const WsConnectionPtr&)>;

    WsConnection(const muduo::net::TcpConnectionPtr& conn, MessageCb onMessage, CloseCb onClose);

    void sendText(const std::string& msg);
    void sendBinary(const std::string& msg);
    void close(uint16_t code = 1000);
    void onData(muduo::net::Buffer* buf);
    void triggerClose();  // called by HttpServer on disconnect

private:
    muduo::net::TcpConnectionPtr conn_;
    MessageCb onMessage_;
    CloseCb   onClose_;
    bool      closed_ { false };
    std::mutex mutex_;
};

struct WsHandlers
{
    std::function<void(const WsConnectionPtr&)>                              onOpen;
    std::function<void(const WsConnectionPtr&, const std::string&, Opcode)> onMessage;
    std::function<void(const WsConnectionPtr&)>                              onClose;
};

struct WsContext
{
    WsConnectionPtr wsConn;
};

// Compute Sec-WebSocket-Accept from client key
std::string computeAcceptKey(const std::string& clientKey);

} // namespace ws
