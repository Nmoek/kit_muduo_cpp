/**
 * @file http_server.h
 * @brief HTTP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 19:55:29
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_SERVER_H__
#define __KIT_HTTP_SERVER_H__

#include "base/noncopyable.h"
#include "net/tcp_server.h"

namespace kit_muduo {
namespace http {

class HttpRequet;
class HttpResponse;

class HttpServer: Noncopyable
{
public:
    using HttpCallBack = std::function<void(const HttpRequet&, HttpResponse&)>;

    HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, TcpServer::Option option = TcpServer::Option::kNoRusePort);

    ~HttpServer() = default;

    void start() { _server.start(); }

    EventLoop *getLoop() const { return _server.getLoop(); }

    void setHttpCallback(const HttpCallBack &cb) { _httpCallBack = std::move(cb); }

    void setThreadNum(int32_t nums) { _server.setThreadNum(nums); }

private:
    void onConnect(TcpConnectionPtr conn);
    void onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receiveTime);

    void handleRequest(TcpConnectionPtr conn, const HttpRequet &req);

private:
    TcpServer _server;
    HttpCallBack _httpCallBack;
};



}
}
#endif