/**
 * @file websocket_context.h
 * @brief websocket上下文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-25 02:36:51
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WEBSOCKET_CONTEXT_H__
#define __KIT_WEBSOCKET_CONTEXT_H__


#include "net/call_backs.h"
#include "net/websocket/websocket_frame.h"
#include <memory>

namespace kit_muduo {

class Buffer;
class TimeStamp;

namespace ws {

enum class WebSocketParseCode
{
    kOk,
    kNeedMore,
    kError,
};

struct WebSocketParseResult
{
    WebSocketParseCode parse_code{WebSocketParseCode::kOk};
    WebSocketFrame frame;
    CloseCode close_code;
    std::string close_message;

    bool ok() const { return parse_code == WebSocketParseCode::kOk; }
    bool needMore() const { return parse_code == WebSocketParseCode::kNeedMore; }

    static WebSocketParseResult Error(CloseCode close_code, const std::string &close_message)
    {
        return {
            .parse_code = WebSocketParseCode::kError,
            .close_code = close_code,
            .close_message = close_message,
        };
    }

    static WebSocketParseResult NeedMore()
    {
        return {
            .parse_code = WebSocketParseCode::kNeedMore
        };
    }

};


class WebSocketContext
{
public:
    explicit WebSocketContext(WebSocketSessionPtr session, size_t max_single_frame_payload_bytes = kDefaultMaxFrameBytes);
    ~WebSocketContext() = default;

    // 特别注意：由于当前还未支持分片重组解析，因为上下文的语义较弱，但必须保持现有骨架
    WebSocketParseResult parseFrame(kit_muduo::Buffer &buf, kit_muduo::TimeStamp receive_time);

    WebSocketSessionPtr lockSession() const { return weak_session_.lock(); }

public:
    static constexpr size_t kDefaultMaxFrameBytes = 10 * 1024 * 1024; // 默认单帧最大负载10M
    static constexpr size_t kUint16LengthPolicy = 126;
    static constexpr size_t kUint64LengthPolicy = 127;

private:
    WebSocketParseResult parseOneFrame(kit_muduo::Buffer &buf);

    /**
     * @brief 注意：V1版本拒绝策略 非websocket标准策略
     * @param fin 
     * @param opcode 
     * @param payload_len 
     * @return WebSocketParseResult 
     */
    WebSocketParseResult applyV1ClientFramePolicy(bool fin, WebSocketOpcode opcode, uint64_t payload_len);

private:
    std::weak_ptr<WebSocketSession> weak_session_;
    size_t max_single_frame_payload_bytes_{kDefaultMaxFrameBytes};


};



} // ws
} // kit_muduo

#endif //__KIT_WEBSOCKET_CONTEXT_H__