/**
 * @file websocket_session.h
 * @brief websocket连接会话
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-25 19:09:45
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WEBSOCKET_SESSION_H___
#define __KIT_WEBSOCKET_SESSION_H___


#include "net/call_backs.h"
#include "net/inet_address.h"
#include "net/websocket/websocket_frame.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace kit_muduo::ws {

enum class WebSocketSessionState
{
    kOpening,
    kOpen,
    kClosing,
    kFailed,
    kClosed,
};

class WebSocketSession: public std::enable_shared_from_this<WebSocketSession>
{
public:
    using BinaryGroup = std::vector<
        std::shared_ptr<std::vector<uint8_t>>
    >;

    WebSocketSession(uint64_t seesion_id, TcpConnectionPtr conn);
    ~WebSocketSession() = default;

    uint64_t sessionId() const { return session_id_; }

    const InetAddress& peerAddr() const;

    bool isOpen() const { return state_ == WebSocketSessionState::kOpen; }
    bool open() { return transition(WebSocketSessionState::kOpening, WebSocketSessionState::kOpen); }
    bool isClosing() const { return state_ == WebSocketSessionState::kClosing; }

    WebSocketSessionState state() const { return state_; }


    void onMessage(WebSocketContextPtr context, Buffer *buf, TimeStamp receive_time);

    /**
     * @brief 处理传输层TCP断开
     */
    void onTcpDisconnected();
    /**
     * @brief 处理传输层TCP写入完成
     */
    void onTcpWriteComplete();

    void sendText(const std::string &payload);
    void sendBinary(const std::vector<uint8_t> &payload);

    void sendMessageGroup(const std::string &json_msg, const BinaryGroup& binary_frames);

    void close(CloseCode close_code, const std::string &reason = "");
    void fail(CloseCode close_code, const std::string &reason);

    void setWSTextMessageCb(WSTextMessageCb cb) { text_cb_ = std::move(cb); }
    void setCloseCb(WSClosedCb cb) { close_cb_ = std::move(cb); }
    void setWSErrorCb(WSErrorCb cb) { error_cb_ = std::move(cb); }
    void setWSWriteCompleteCb(WSWriteCompleteCb cb){ write_complete_cb_ = std::move(cb); }

    void setWSClearCb(WSClearCb cb) { clear_cb_ = std::move(cb); }

public:
    /// @brief 默认单帧最大负载10M
    static constexpr size_t kDefaultMaxFrameBytes = 10 * 1024 * 1024;
     /// @brief 帧头部最大长度
    static constexpr size_t kMaxFrameHeaderBytes = 14;
    /// @brief 关闭握手超时 默认3s
    static constexpr int64_t kCloseHandshakeTimeoutMs = 3000;

private:
    bool transition(WebSocketSessionState from, WebSocketSessionState to);
    void sendDataFrame(WebSocketOpcode opcode, const std::vector<uint8_t> &payload);
    void sendControlFrame(WebSocketOpcode opcode, const std::vector<uint8_t> &payload);

    void handleFrame(const WebSocketFrame &frame);
    void closeInLoop(CloseCode close_code, const std::string &reason);
    void realClose(CloseCode close_code, const std::string &reason);

    /**
     * @brief 处理幂等关闭
     */
    void fireCloseOnce();
    /**
     * @brief 处理幂等清理session
     */
    void clearOnce();

private:
    /// @brief session Id
    uint64_t session_id_{0};
    /// @brief TCP连接强引用
    TcpConnectionPtr conn_;
    /// @brief 会话状态
    std::atomic<WebSocketSessionState> state_{WebSocketSessionState::kOpening};

    std::once_flag close_once_;
    std::once_flag clear_once_;

    /// @brief 关闭握手定时器 默认3s
    TimerPtr close_timer_;

    WSTextMessageCb text_cb_;
    WSClosedCb close_cb_;
    WSErrorCb error_cb_;
    WSWriteCompleteCb write_complete_cb_;
    WSClearCb clear_cb_; // 注意:只有这个是内部回调
};









}
#endif // __KIT_WEBSOCKET_SESSION_H___