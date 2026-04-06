#include "../../include/ws/WsFrame.h"
#include <cstring>
#include <stdexcept>

namespace ws
{

bool parseFrame(muduo::net::Buffer* buf, WsFrame& frame)
{
    const size_t readable = buf->readableBytes();
    if (readable < 2) return false;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(buf->peek());
    frame.fin    = (data[0] & 0x80) != 0;
    frame.opcode = static_cast<Opcode>(data[0] & 0x0F);

    bool masked      = (data[1] & 0x80) != 0;
    uint64_t payloadLen = data[1] & 0x7F;
    size_t headerLen = 2;

    if (payloadLen == 126)
    {
        if (readable < 4) return false;
        payloadLen = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        headerLen = 4;
    }
    else if (payloadLen == 127)
    {
        if (readable < 10) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i)
            payloadLen = (payloadLen << 8) | data[2 + i];
        headerLen = 10;
    }

    size_t maskLen = masked ? 4 : 0;
    size_t totalLen = headerLen + maskLen + payloadLen;
    if (readable < totalLen) return false;

    frame.payload.resize(payloadLen);
    const uint8_t* src = data + headerLen + maskLen;
    if (masked)
    {
        const uint8_t* mask = data + headerLen;
        for (uint64_t i = 0; i < payloadLen; ++i)
            frame.payload[i] = static_cast<char>(src[i] ^ mask[i % 4]);
    }
    else
    {
        if (payloadLen > 0)
        {
            std::memcpy(&frame.payload[0], src, payloadLen);
        }
    }

    buf->retrieve(totalLen);
    return true;
}

std::string buildFrame(Opcode opcode, const std::string& payload)
{
    std::string frame;
    frame += static_cast<char>(0x80 | static_cast<uint8_t>(opcode));

    size_t len = payload.size();
    if (len < 126)
    {
        frame += static_cast<char>(len);
    }
    else if (len < 65536)
    {
        frame += static_cast<char>(126);
        frame += static_cast<char>((len >> 8) & 0xFF);
        frame += static_cast<char>(len & 0xFF);
    }
    else
    {
        frame += static_cast<char>(127);
        for (int i = 7; i >= 0; --i)
            frame += static_cast<char>((len >> (8 * i)) & 0xFF);
    }
    frame += payload;
    return frame;
}

} // namespace ws
