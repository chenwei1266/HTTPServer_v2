#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>

namespace ws
{

enum class Opcode : uint8_t
{
    Text   = 0x1,
    Binary = 0x2,
    Close  = 0x8,
    Ping   = 0x9,
    Pong   = 0xA
};

struct WsFrame
{
    bool    fin;
    Opcode  opcode;
    std::string payload;
};

// Returns true and consumes buf if a complete frame is available
bool parseFrame(muduo::net::Buffer* buf, WsFrame& frame);

// Builds a server-side (unmasked) frame
std::string buildFrame(Opcode opcode, const std::string& payload);

} // namespace ws
