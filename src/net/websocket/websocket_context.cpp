/**
 * @file websocket_context.cpp
 * @brief websocket上下文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-25 02:37:08
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/endian.h"
#include "net/net_data_converter.h"
#include "net/net_log.h"
#include "net/websocket/websocket_context.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/websocket/websocket_frame.h"
#include <vector>

using namespace kit_muduo;

namespace kit_muduo::ws {

namespace {

bool CheckOpcode(const uint8_t opcode)
{
    switch(static_cast<WebSocketOpcode>(opcode))
    {
        case WebSocketOpcode::kContinuation:
        case WebSocketOpcode::kText:
        case WebSocketOpcode::kBinary:
        case WebSocketOpcode::kClose:
        case WebSocketOpcode::kPing:
        case WebSocketOpcode::kPong:
            return true;
        default:
            return false;
    }
}

/**
 * @brief 判断是否是控制类帧
 * @param opcode 
 * @return true 
 * @return false 
 */
bool IsControlOpcode(const uint8_t opcode)
{
    return opcode >= static_cast<uint8_t>(WebSocketOpcode::kClose);
}

}

WebSocketContext::WebSocketContext(WebSocketSessionPtr session, size_t max_single_frame_payload_bytes)
    :weak_session_(session)
    ,max_single_frame_payload_bytes_(max_single_frame_payload_bytes)
{

}

WebSocketParseResult WebSocketContext::parseFrame(Buffer &buf, TimeStamp receive_time)
{
    auto result = parseOneFrame(buf);
    if(result.ok())
    {
        result.frame.receive_time = std::move(receive_time);
    }
    return result;
}


WebSocketParseResult WebSocketContext::parseOneFrame(Buffer &buf)
{
    WebSocketParseResult result;
    constexpr const int32_t kMaskBytesLen = 4;

    const auto &data = buf.lookAllAsDataUint8();

    if(data.size() <= 2)
    {
        WS_F_INFO("websocket data need more! %ld\n", data.size());
        return WebSocketParseResult::NeedMore();
    }


    const uint8_t b0 = data.at(0);
    const uint8_t b1 = data.at(1);

    const bool is_fin = (b0 & 0x80) != 0;
    const bool is_rsv =  (b0 & 0x70) != 0;
    const uint8_t raw_opcode = b0 & 0x0F;
    const bool is_masked = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    if(is_rsv)
    {
        return WebSocketParseResult::Error(CloseCode::kProtocolError, "rsv bits unsupported");
    }
    if(!CheckOpcode(raw_opcode))
    {
        return WebSocketParseResult::Error(CloseCode::kProtocolError, "opcode unsupported");
    }
    if(!is_masked)
    {
        return WebSocketParseResult::Error(CloseCode::kProtocolError, "client frame must be masked");
    }

    // 关键: 这个偏移量决定后续如何解析
    // 负载长度解析
    size_t offset = 2;

    if(kUint16LengthPolicy == payload_len)
    {
        offset += sizeof(uint16_t);
        if(offset > data.size())
        {
            return WebSocketParseResult::NeedMore();
        }
        // 注意: 网络大端转换
        payload_len = kit_muduo::BytesToValue<uint16_t>(std::vector<uint8_t>(data.begin() + 2, data.begin() + offset), !KIT_IS_LOCAL_BIG_ENDIAN());
    }
    else if(kUint64LengthPolicy == payload_len)
    {
        offset += sizeof(uint64_t);
        if(offset > data.size())
        {
            return WebSocketParseResult::NeedMore();
        }
        // 注意: 网络大端转换
        payload_len = kit_muduo::BytesToValue<uint64_t>(std::vector<uint8_t>(data.begin() + 2, data.begin() + offset), !KIT_IS_LOCAL_BIG_ENDIAN());
    }



    if(IsControlOpcode(raw_opcode))
    {
        // 控制类帧不允许分片
        if(!is_fin)
        {
            return WebSocketParseResult::Error(CloseCode::kProtocolError, "control frame disallow fragmented");
        }
        // 控制类帧不允许拓展长度
        if(payload_len > 125)
        {
            return WebSocketParseResult::Error(CloseCode::kProtocolError, "control frame too large");
        }
    }

    auto v1_policy_result = applyV1ClientFramePolicy(is_fin, static_cast<WebSocketOpcode>(raw_opcode), payload_len);
    if(!v1_policy_result.ok())
    {
        WS_F_ERROR("applyV1ClientFramePolicy error!\n");
        return v1_policy_result;
    }

    if(offset + kMaskBytesLen + payload_len > data.size())
    {
        return WebSocketParseResult::NeedMore();
    }
    // 掩码值解析(注意 这里不需要解析为uint32 只是4个raw bytes)
    std::vector<uint8_t> mask(data.begin() + offset, data.begin() + offset + kMaskBytesLen);
    offset += kMaskBytesLen;

    result.parse_code = WebSocketParseCode::kOk;
    result.frame.fin = is_fin;
    result.frame.opcode = static_cast<WebSocketOpcode>(raw_opcode);
    result.frame.payload.resize(payload_len);

    // 掩码反解析
    for(int i = 0;i < payload_len;++i)
    {
        result.frame.payload[i] = data.at(offset + i) ^ mask[i % kMaskBytesLen];
    }
    offset += payload_len;

    // 缓冲区消费
    buf.reset(offset);

    return result;
}

WebSocketParseResult WebSocketContext::applyV1ClientFramePolicy(bool fin, WebSocketOpcode opcode, uint64_t payload_len)
{
    // v1版本不支持分片重组
    if(!fin && !IsControlOpcode(static_cast<const uint8_t>(opcode)))
    {
        return WebSocketParseResult::Error(CloseCode::kProtocolError, "v1 fragmented message unsupported");
    }
    // v1不支持分片重组 不应该出现延续帧
    if(WebSocketOpcode::kContinuation == opcode)
    {
        return WebSocketParseResult::Error(CloseCode::kProtocolError, "v1 continuation unsupported");
    }
    // v1不支持分片重组 不应该出现延续帧
    if(WebSocketOpcode::kBinary == opcode)
    {
        return WebSocketParseResult::Error(CloseCode::kUnsupportedDataType, "v1 binary unsupported");
    }
    // v1有单帧最大负载限制
    if(payload_len > max_single_frame_payload_bytes_)
    {
        return WebSocketParseResult::Error(CloseCode::kMessageLarge, "v1 frame too large <=10m");
    }
    return WebSocketParseResult{};
}


}