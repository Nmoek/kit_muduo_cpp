/**
 * @file websocket_server.cpp
 * @brief WebSocket服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-29 20:07:28
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/call_backs.h"
#include "net/http/http_servlet.h"
#include "net/http/http_util.h"
#include "net/net_log.h"
#include "net/websocket/websocket_server.h"
#include "net/websocket/websocket_util.h"
#include "net/websocket/websocket_session.h"
#include "net/websocket/websocket_context.h"
#include "net/http/http_context.h"
#include "net/http/http_response.h"
#include "net/tcp_connection.h"
#include "net/event_loop.h"

#include <exception>
#include <memory>
#include <mutex>
#include <string>

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace kit_muduo::ws {

namespace {

/**
 * @brief 升级请求是否合法
 * @param req 
 * @param reason 
 * @return true 
 * @return false 
 */
inline bool CheckWebSocketUpgradeRequest(HttpRequestPtr& req, std::string &reason)
{
    auto fail = [&reason](const std::string &msg) {
        reason = msg;
        return false;
    };

    if(req->method().toInt() != http::HttpRequest::Method::kGet)
    {
        return fail("websocket upgrade method must be GET");
    }
    if(req->version().toInt() != http::Version::kHttp11)
    {
        return fail("websocket upgrade requires HTTP/1.1");
    }
    if(!http::HeaderContainsToken(req->getHeader("Connection"), "Upgrade"))
    {
        return fail("Connection header missing Upgrade token");
    }
    if(!http::IsHeaderName(req->getHeader("Upgrade"), "websocket"))
    {
        return fail("Upgrade header must be websocket");
    }
    if(req->getHeader("Sec-WebSocket-Version") != "13")
    {
        return fail("Sec-WebSocket-Version must be 13");
    }
    if(!IsValidWebSocketKey(req->getHeader("Sec-WebSocket-Key")))
    {
        return fail("Sec-WebSocket-Key invalid");
    }
    return true;
}
}

std::atomic_uint64_t WebSocketServer::s_next_session_id{1};

WebSocketSessionPtr WebSocketServer::getSession(uint64_t session_id)
{
    std::unique_lock<std::mutex> lock(mtx);
    auto it = sessions_.find(session_id);
    return it == sessions_.end() ? nullptr : it->second;
}

void WebSocketServer::removeSession(uint64_t session_id)
{
    std::unique_lock<std::mutex> lock(mtx);
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        sessions_.erase(it);
    }
}

void WebSocketServer::handleUpgrade(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx, WsOnCb on_cb) 
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    // 1. 校验升级字段合法性
    std::string reason;
    if(!CheckWebSocketUpgradeRequest(req, reason))
    {
        WS_F_ERROR("http upgrade request invalid: %s\n", reason.c_str());

        http::BadRequest400Servlet::Handle(conn, ctx);
        return;
    }
    // 2. 创建WebSocketSessioin
    auto id = s_next_session_id.fetch_add(1);
    auto session = std::make_shared<WebSocketSession>(id, conn);
    if(!session)
    {
        WS_F_ERROR("websocket session create error! %s \n", conn->peerAddr().toIpPort().c_str());

        ServerErr500Servlet::Handle(nullptr, ctx);
        resp->resetBodyData();
        return;
    }


    // 3. 绑定协议层所必须回调
    session->setWSClearCb([this](uint64_t session_id){
        WS_F_DEBUG("websocket del session  %ld\n", session_id);
        removeSession(session_id);
    });

    // 4. 执行用户业务的准备操作
    if(on_cb)
    {
        try {
            if(!on_cb(session, ctx))
            {
                WS_F_ERROR("websocket session on callback error!\n");
                ServerErr500Servlet::Handle(nullptr, ctx);
                resp->resetBodyData();
                return;
            }
        } catch (const std::exception &e) {
            WS_F_ERROR("websocket session on callback exception: %s\n", e.what());
            ServerErr500Servlet::Handle(nullptr, ctx);
            resp->resetBodyData();
            return;
        }

    }

    // 5. 替换TcpContext 
    bindSessionToConnection(conn, session);

    // 将Session置为kOpen状态
    if(!session->open())
    {
        WS_F_ERROR("websocket session open error! %s \n", conn->peerAddr().toIpPort().c_str());

        ServerErr500Servlet::Handle(nullptr, ctx);
        resp->resetBodyData();
        return;
    }

    addSession(session);

    // 101响应返回
    const std::string accept = BuildWebSocketAcceptKey(
        req->getHeader("Sec-WebSocket-Key"));

    resp->setUpgrade(true);
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k101SwitchingProtocols);
    resp->setSecWebSocketAccept(accept);

    return;
}

void WebSocketServer::drainRemainingWebSocketBytes(TcpConnectionPtr &conn,
    Buffer *buf,
    TimeStamp receive_time)
{
    if(buf && buf->readableBytes() > 0)
    {
        conn->getLoop()->queueInLoop([=](){
            onMessage(conn, buf, receive_time);
        });
    }
}

void WebSocketServer::onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receive_time)
{
    std::shared_ptr<WebSocketContext> context = std::static_pointer_cast<WebSocketContext>(conn->getContext());
    if(nullptr == context)
    {
        WS_F_ERROR("websocket context is null!\n");
        return;
    }

    auto session = context->lockSession();
    if(!session)
    {
        if(conn->connected())
        {
            conn->shutdown();
        }
        return;
    }

    session->onMessage(context, buf, receive_time);
}


void WebSocketServer::addSession(WebSocketSessionPtr session)
{
    std::unique_lock<std::mutex> lock(mtx);
    auto id = session->sessionId();
    auto it = sessions_.find(id);
    if(it != sessions_.end())
    {
        WS_F_WARN("websocket session exist: %ld \n",id);
        return;
    }
    sessions_[id] = session;
}

void WebSocketServer::bindSessionToConnection(TcpConnectionPtr conn, WebSocketSessionPtr session)
{
    auto context = std::make_shared<WebSocketContext>(session);
    conn->setContext(context);

    conn->setConnectionCallback([](const TcpConnectionPtr& conn){
        std::shared_ptr<WebSocketContext> context = std::static_pointer_cast<WebSocketContext>(conn->getContext());
        if(nullptr == context)
        {
            WS_F_ERROR("websocket context is null!\n");
            return;
        }

        auto session = context->lockSession();
        if(!session)
        {
            WS_F_ERROR("websocket session is null!\n");
            return;
        }
        if(!conn->connected())
        {
            session->onTcpDisconnected();
        }
    });

    conn->setMessageCallback(std::bind(&WebSocketServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));


}









}