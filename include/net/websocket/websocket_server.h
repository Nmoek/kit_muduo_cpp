/**
 * @file websocket_server.h
 * @brief WebSocket服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-28 21:05:08
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WEBSOCKET_SERVER_H__
#define __KIT_WEBSOCKET_SERVER_H__

#include "net/call_backs.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace kit_muduo::ws {


class WebSocketServer
{
public:

    WebSocketSessionPtr getSession(uint64_t session_id);
    void removeSession(uint64_t session_id);


    void handleUpgrade(TcpConnectionPtr conn, HttpContextPtr ctx, WsPrepareCb preprae_cb);

    void drainRemainingWebSocketBytes(TcpConnectionPtr &conn, Buffer *buf, TimeStamp receive_time);

    void onOpen(TcpConnectionPtr conn);

private:
    void onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receive_time);

    void addSession(WebSocketSessionPtr session);

    void bindSessionToConnection(TcpConnectionPtr conn, WebSocketSessionPtr session);

private:
    static std::atomic_uint64_t s_next_session_id;

private:
    using SessionMap = std::unordered_map<uint64_t, WebSocketSessionPtr>;
    std::mutex mtx;
    SessionMap sessions_;
};



} // ws
#endif //__KIT_WEBSOCKET_SERVER_H__