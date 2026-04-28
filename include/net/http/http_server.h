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
#include "net/http/http_servlet.h"
#include "net/http/http_request.h"
#include "net/call_backs.h"
#include "base/thread_pool.h"

#include <string>



namespace kit_muduo::http {

class RouterMatcher;

class HttpServer: Noncopyable
{
public:
    using HttpCallBack = std::function<void(TcpConnectionPtr, HttpContextPtr)>;

    HttpServer(kit_muduo::EventLoop *loop, const InetAddress &addr, const std::string &name, bool isPool = true, TcpServer::Option option = TcpServer::Option::kNoRusePort);

    ~HttpServer() = default;

    void start();

    kit_muduo::EventLoop *getLoop() const { return _server.getLoop(); }

    void setHttpCallback(const HttpCallBack &cb) { _httpCallBack = std::move(cb); }

    void setThreadNum(int32_t nums) { _server.setThreadNum(nums); }

    std::shared_ptr<HttpServletDispatch> getServletDispatch() { return _dispatch; }

    TcpConnectionPtr getConnection(const std::string &name) { return _server.getConnection(name); }


    void addRoute(const std::string &url, HttpServlet::Ptr svl);

    void addRoute(const std::string &url, HttpServlet::Ptr svl, std::shared_ptr<RouterMatcher> matcher);

    void addRoute(const HttpRequest::Method method, const std::string &url, const FunctionServlet::CallBack &cb);
    
    void delRoute(const std::string &url, const HttpRequest::Method method);
    
    void Get(const std::string &url, HttpServlet::Ptr svl);
    void Get(const std::string &url, const FunctionServlet::CallBack &cb);

    void Post(const std::string &url, HttpServlet::Ptr svl);
    void Post(const std::string &url, const FunctionServlet::CallBack &cb);

    void GetAndPost(const std::string &url, HttpServlet::Ptr svl);
    void GetAndPost(const std::string &url, const FunctionServlet::CallBack &cb);
    
    void Delete(const std::string &url, HttpServlet::Ptr svl);
    void Delete(const std::string &url, const FunctionServlet::CallBack &cb);


private:
    void onConnect(TcpConnectionPtr conn);
    void onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receiveTime);

    // http服务器默认处理函数
    void handleRequest(TcpConnectionPtr conn, HttpContextPtr ctx);

private:
    TcpServer _server;
    HttpCallBack _httpCallBack;
    ThreadPool _businessThreadPool;// 注意: 这个是http业务额外的线程池，和处理网络连接evnet_loop的线程池侧重点不一样
    std::shared_ptr<HttpServletDispatch> _dispatch;
    bool _isPool;   // 是否使用线程池
};



}

#endif