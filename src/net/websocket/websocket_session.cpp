/**
 * @file websocket_session.cpp
 * @brief websocket连接会话
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-25 19:09:22
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/call_backs.h"
#include "net/net_log.h"
#include "net/websocket/websocket_session.h"
#include "net/websocket/websocket_context.h"
#include "net/buffer.h"
#include "net/tcp_connection.h"
#include "net/websocket/websocket_frame.h"
#include "net/event_loop.h"

#include <atomic>
#include <mutex>
#include <vector>

using namespace kit_muduo;

namespace kit_muduo::ws {

namespace {

void AppendWebSocketFrameBytes(std::vector<uint8_t>& out, WebSocketOpcode opcode, const std::vector<uint8_t> &payload)
{
    const auto& frame = BuildWebSocketFrameBytes(opcode, payload);
    out.insert(out.end(), frame.begin(), frame.end());
}

inline bool ChechkControlFrame(WebSocketOpcode opcode)
{
    return WebSocketOpcode::kClose == opcode
        || WebSocketOpcode::kPing == opcode
        || WebSocketOpcode::kPong == opcode;
}

}

WebSocketSession::WebSocketSession(uint64_t seesion_id, TcpConnectionPtr conn)
    :session_id_(seesion_id)
    ,conn_(std::move(conn))
{

}


const InetAddress& WebSocketSession::peerAddr() const 
{ 
    return conn_->peerAddr();
}

void WebSocketSession::onMessage(WebSocketContextPtr context, Buffer *buf, TimeStamp receive_time)
{

    while(buf->readableBytes() > 0)
    {
        auto ws_result = context->parseFrame(*buf, receive_time);

        // 未解析完 等待更多数据
        if(ws_result.needMore())
        {
            WS_F_DEBUG("websocket data not complete! %lu \n", buf->readableBytes());
            return;
        }
        if(!ws_result.ok())
        {
            WS_F_ERROR("websocket frame parse errror: %s\n", ws_result.close_message.c_str());

            fail(ws_result.close_code, ws_result.close_message);
            return;
        }

        handleFrame(ws_result.frame);
    }


}


void WebSocketSession::onTcpDisconnected()
{
    realClose(CloseCode::kExceptionalShutdown, "tcp peer disconnected");

}

void WebSocketSession::onTcpWriteComplete()
{
    if(write_complete_cb_)
    {
        write_complete_cb_(shared_from_this());
    }
}

void WebSocketSession::sendText(const std::string &payload)
{
    sendDataFrame(WebSocketOpcode::kText, std::vector<uint8_t>(payload.begin(), payload.end()));
}
void WebSocketSession::sendBinary(const std::vector<uint8_t> &payload)
{
    sendDataFrame(WebSocketOpcode::kBinary, payload);
}

void WebSocketSession::sendMessageGroup(const std::string &json_msg, const BinaryGroup& binary_frames)
{
    if(!isOpen())
    {
        WS_F_ERROR("webscoket session disconnected\n");
        return;
    }
    size_t reserve_bytes = json_msg.size() + kMaxFrameHeaderBytes;
    for(const auto &frame : binary_frames)
    {
        if(frame)
        {
            reserve_bytes += frame->size() + kMaxFrameHeaderBytes;
        }
        else
        {
            WS_F_ERROR("binray frame data is null!\n");
            return;
        }
    }

    std::vector<uint8_t> out;
    out.reserve(reserve_bytes);

    AppendWebSocketFrameBytes(out, WebSocketOpcode::kText, std::vector<uint8_t>(json_msg.begin(), json_msg.end()));

    for(const auto &frame : binary_frames)
    {
        AppendWebSocketFrameBytes(out, WebSocketOpcode::kBinary, *frame);
    }

    conn_->send(out);
}

void WebSocketSession::close(CloseCode close_code, const std::string &reason)
{
    conn_->getLoop()->runInLoop([session = shared_from_this(),
        close_code,
        reason](){
            session->closeInLoop(close_code, reason);
    });
}

void WebSocketSession::closeInLoop(CloseCode close_code, const std::string &reason)
{

    if(!transition(WebSocketSessionState::kOpen, WebSocketSessionState::kClosing))
    {
        WS_F_ERROR("webscoekt session transition state error: open --> closing \n");
        return;
    }
    sendControlFrame(WebSocketOpcode::kClose, BuildClosePayload(close_code, reason));

    // 如果在握手超时内没有收到关闭帧回复 则强行关闭
    close_timer_ = conn_->getLoop()->runAfter(kCloseHandshakeTimeoutMs, [session = shared_from_this()](){
        session->realClose(CloseCode::kNoStatusCode, "close handshake timeout");
    });
}

void WebSocketSession::realClose(CloseCode close_code, const std::string &reason)
{
    if(WebSocketSessionState::kClosed == state_)
    {
        WS_F_DEBUG("websocket session has closed\n");
        return;
    }

    // 如果正常收到关闭握手情况下取消定时器
    if(close_timer_)
    {
        conn_->getLoop()->cancel(close_timer_);
        close_timer_.reset();
    }

    if(conn_->connected())
    {
        conn_->shutdown();
    }

    fireCloseOnce(); // 关闭前用户回调
    state_.store(WebSocketSessionState::kClosed, std::memory_order_release);
    clearOnce();    // 清理内部回调
}


void WebSocketSession::fail(CloseCode close_code, const std::string &reason)
{
    const auto old = state();
    if(old == WebSocketSessionState::kClosed
        || old == WebSocketSessionState::kClosing
        || old == WebSocketSessionState::kFailed)
    {
        return;
    }

    if(!transition(old, WebSocketSessionState::kFailed))
    {
        WS_F_ERROR("webscoekt session transition state error: %d --> kFailed \n", static_cast<int32_t>(old));
        return;
    }

    if(error_cb_)
    {
        error_cb_(shared_from_this(), close_code, reason);
    }

    sendControlFrame(WebSocketOpcode::kClose, BuildClosePayload(close_code, reason));

    realClose(close_code, reason);
}

bool WebSocketSession::transition(WebSocketSessionState from, WebSocketSessionState to)
{
    return state_.compare_exchange_strong(from, to, std::memory_order_acq_rel, std::memory_order_acquire);
}

void WebSocketSession::sendDataFrame(WebSocketOpcode opcode, const std::vector<uint8_t> &payload)
{
    if(WebSocketOpcode::kText != opcode && WebSocketOpcode::kBinary != opcode)
    {
        WS_F_ERROR("opcode invalid! %d\n", static_cast<int32_t>(opcode));
        return;
    }

    if(!isOpen())
    {
        WS_F_ERROR("websocket session disconnected!\n");
        return;
    }

    conn_->send(BuildWebSocketFrameBytes(opcode, payload));
}

void WebSocketSession::sendControlFrame(WebSocketOpcode opcode, const std::vector<uint8_t> &payload)
{
    if(!ChechkControlFrame(opcode))
    {
        WS_F_ERROR("opcode invalid! %d\n", static_cast<int32_t>(opcode));
        return;
    }

    if(payload.size() > 125)
    {
        WS_F_ERROR("payload size >125! %ld\n", payload.size());
        return;
    }

    conn_->send(BuildWebSocketFrameBytes(opcode, payload));
}

void WebSocketSession::handleFrame(const WebSocketFrame &frame)
{
    if(WebSocketSessionState::kClosing == state_.load() && WebSocketOpcode::kClose != frame.opcode)
    {
        WS_F_INFO("websocket session closing... %s\n", conn_->peerAddr().toIpPort().c_str());
        return;
    }

    WS_F_DEBUG("websocket handle frame[%s] ====> opcode[%d] payload size[%ld]\n", conn_->peerAddr().toIpPort().c_str(), static_cast<int32_t>(frame.opcode), frame.payload.size());

    switch (frame.opcode) 
    {
        case WebSocketOpcode::kText:
        {
            if(text_cb_)
            {
                text_cb_(shared_from_this(), std::string(frame.payload.begin(),
                    frame.payload.end()));
            }

            break;
        }
        case WebSocketOpcode::kPing:
        {
            WS_F_DEBUG("recv client ping... %s\n", conn_->peerAddr().toIpPort().c_str());

            sendControlFrame(WebSocketOpcode::kPong, frame.payload);
            break;
        }
        case WebSocketOpcode::kPong:
        {
            // v1版本 dothing
            WS_F_INFO("recv client ping... %s\n", conn_->peerAddr().toIpPort().c_str());

            break;
        }
        case WebSocketOpcode::kBinary:
        {
            fail(CloseCode::kUnsupportedDataType, "v1 binary unsupported");
            break;
        }
        case WebSocketOpcode::kClose:
        {
            WS_F_INFO("client peer close... %s\n", conn_->peerAddr().toIpPort().c_str());

            sendControlFrame(WebSocketOpcode::kClose, BuildClosePayload(CloseCode::kNormalShutdown, "peer close"));

            realClose(CloseCode::kNormalShutdown, "peer close");
            break;
        }
        default:
        {
            fail(CloseCode::kProtocolError, "unsupported opcode");
        }
    }
}

void WebSocketSession::fireCloseOnce()
{
    std::call_once(close_once_, [sesion = shared_from_this()](){
        if(sesion->close_cb_)
        {
            sesion->close_cb_(sesion);
        }
    });
}


void WebSocketSession::clearOnce()
{
    std::call_once(clear_once_, [sesion = shared_from_this()](){
        if(sesion->clear_cb_)
        {
            sesion->clear_cb_(sesion->session_id_);
        }
    });
}

}