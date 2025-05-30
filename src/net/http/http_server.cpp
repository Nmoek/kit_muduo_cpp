/**
 * @file http_server.cpp
 * @brief HTTP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 20:03:17
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_server.h"
#include "net/http/http_context.h"
#include "net/net_log.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"

namespace kit_muduo {
namespace http {


HttpServer::HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, TcpServer::Option option)
    :_server(loop, addr, name, option)
    ,_httpCallBack(nullptr)
{
    _server.setConnectionCallback(std::bind(&HttpServer::onConnect, this, std::placeholders::_1));

    _server.setMessageCallback(std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}


void HttpServer::onConnect(TcpConnectionPtr conn)
{
    if(conn->connected())
    {
        conn->setContext(std::make_shared<HttpContext>());
    }

}
void HttpServer::onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receiveTime)
{
    std::shared_ptr<HttpContext> context = std::static_pointer_cast<HttpContext>(conn->getContext());
    if(nullptr == context)
    {
        HTTP_ERROR() << "http context is null!" << std::endl;
        return;
    }

    if(!context->parseRequest(*buf, receiveTime))
    {
        HTTP_ERROR() << "http request parse error! " << std::endl;
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
        conn->shutdown();
    }

    if(context->gotAll())
    {
        handleRequest(conn, context->request());
        context->reset();
    }

}

void HttpServer::handleRequest(TcpConnectionPtr conn, const HttpRequet &req)
{
    const std::string &connection = req.getHeader("Connection");
    bool closed = (connection == "close")
            || (Version::kHttp10 == req.version()() && connection != "Keep-Alive");
    HttpResponse resp;
    resp.setConnectionClosed(closed);
    _httpCallBack(req, resp);

    const std::string &data = resp.toString();
    std::cout << "resp: " << data << std::endl;
    conn->send(data);
    if(resp.connectionClosed())
    {
        conn->shutdown();
    }
}

}
}