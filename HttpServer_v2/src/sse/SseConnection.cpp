#include "../../include/sse/SseConnection.h"

namespace sse
{

SseConnection::SseConnection(const muduo::net::TcpConnectionPtr& conn)
    : conn_(conn)
{}

void SseConnection::send(const std::string& data, const std::string& event)
{
    if (!connected()) return;
    std::string msg;
    if (!event.empty())
        msg += "event: " + event + "\n";
    msg += "data: " + data + "\n\n";
    conn_->send(msg);
}

void SseConnection::close()
{
    if (connected())
        conn_->shutdown();
}

bool SseConnection::connected() const
{
    return conn_ && conn_->connected();
}

} // namespace sse
