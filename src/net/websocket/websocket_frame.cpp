/**
 * @file websocket_frame.cpp
 * @brief websocket 帧数据序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-24 20:32:11
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/endian.h"
#include "net/net_data_converter.h"
#include "net/net_log.h"
#include "net/websocket/websocket_frame.h"

#include <vector>

using namespace kit_muduo;

namespace kit_muduo::ws {


std::vector<uint8_t> BuildWebSocketFrameBytes(WebSocketOpcode opcode, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> bytes(2);
    // 注意: 当前V1 默认都是最后一帧
    bytes[0] |= (1 << 7);
    bytes[0] |= (static_cast<uint8_t>(opcode) & 0x0F);
    if(payload.size() <= 125)
    {
        bytes[1] = static_cast<uint8_t>(payload.size());
    }
    else if(payload.size() <= 0xFFFF)
    {
        bytes[1] = 126;
        uint16_t length = static_cast<uint16_t>(payload.size());SwapToBigEndian(length);

        auto p = reinterpret_cast<uint8_t*>(&length);
        std::vector<uint8_t>  length_bytes(p, p + sizeof(uint16_t));


        bytes.insert(bytes.end(), length_bytes.begin(), length_bytes.end());
    }
    else
    {
        bytes[1] = 127;
        uint64_t length = static_cast<uint64_t>(payload.size());SwapToBigEndian(length);

        auto p = reinterpret_cast<uint8_t*>(&length);
        std::vector<uint8_t>  length_bytes(p, p + sizeof(uint64_t));


        bytes.insert(bytes.end(), length_bytes.begin(), length_bytes.end());
    }

    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

std::vector<uint8_t> BuildWebSocketFrameBytes(WebSocketOpcode opcode, const std::string &payload)
{
    return BuildWebSocketFrameBytes(opcode, std::vector<uint8_t>(payload.begin(), payload.end()));
}


std::vector<uint8_t> BuildClosePayload(CloseCode code, const std::string &reason)
{
    std::vector<uint8_t> payload;
    uint16_t code_val = static_cast<uint16_t>(code);

    const auto& code_bytes = ValueToBytes<uint16_t>(SwapToBigEndian(code_val));

    payload.insert(payload.end(), code_bytes.begin(), code_bytes.end());

    
    payload.insert(payload.end(), reason.begin(), reason.begin() + std::min(reason.size(), (size_t)123));
    return payload;
}












}
