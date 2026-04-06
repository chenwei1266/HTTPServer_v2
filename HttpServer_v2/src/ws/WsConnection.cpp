#include "../../include/ws/WsConnection.h"

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <muduo/base/Logging.h>

namespace ws
{

static std::string base64Encode(const unsigned char* data, size_t len)
{
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, mem);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

std::string computeAcceptKey(const std::string& clientKey)
{
    static const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = clientKey + GUID;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), hash);
    return base64Encode(hash, SHA_DIGEST_LENGTH);
}

WsConnection::WsConnection(const muduo::net::TcpConnectionPtr& conn,
                           MessageCb onMessage,
                           CloseCb onClose)
    : conn_(conn)
    , onMessage_(std::move(onMessage))
    , onClose_(std::move(onClose))
{}

void WsConnection::sendText(const std::string& msg)
{
    if (conn_->connected())
        conn_->send(buildFrame(Opcode::Text, msg));
}

void WsConnection::sendBinary(const std::string& msg)
{
    if (conn_->connected())
        conn_->send(buildFrame(Opcode::Binary, msg));
}

void WsConnection::close(uint16_t code)
{
    std::string payload;
    payload += static_cast<char>((code >> 8) & 0xFF);
    payload += static_cast<char>(code & 0xFF);
    if (conn_->connected())
        conn_->send(buildFrame(Opcode::Close, payload));
    conn_->shutdown();
}

void WsConnection::onData(muduo::net::Buffer* buf)
{
    WsFrame frame;
    while (parseFrame(buf, frame))
    {
        switch (frame.opcode)
        {
        case Opcode::Ping:
            conn_->send(buildFrame(Opcode::Pong, frame.payload));
            break;
        case Opcode::Close:
            conn_->send(buildFrame(Opcode::Close, frame.payload));
            conn_->shutdown();
            break;
        case Opcode::Text:
        case Opcode::Binary:
            if (onMessage_)
                onMessage_(shared_from_this(), frame.payload, frame.opcode);
            break;
        default:
            break;
        }
    }
}

void WsConnection::triggerClose()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!closed_)
    {
        closed_ = true;
        if (onClose_)
            onClose_(shared_from_this());
    }
}

} // namespace ws
