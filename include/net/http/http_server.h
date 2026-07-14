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
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/inet_address.h"
#include "net/tcp_server.h"
#include "net/http/http_servlet.h"
#include "net/http/http_request.h"
#include "net/call_backs.h"
#include "base/thread_pool.h"

#include <string>



namespace kit_muduo::http {

enum class HttpDispatchResult
{
    kContinueHttp,      // 普通 HTTP，请重置 HttpContext，继续处理后续 HTTP 请求
    kProtocolUpgraded,  // 已切换协议，不要重置 HttpContext，不要继续 HTTP parse
    kClose             // 已发送响应并准备关闭
};

class HttpServer: Noncopyable
{
public:
    using HttpCallBack = std::function<void(TcpConnectionPtr, HttpContextPtr)>;
    using StopCallBack = TcpServer::StopCb;

    struct AuthCheckResult
    {
        bool ok{true};
        int32_t http_status{200};
        std::string message;
        bool redirect_to_login{false};
    };
    using AuthCallback = std::function<AuthCheckResult(HttpContextPtr)>;

    struct BusinessThreadPoolConfig
    {
        int32_t threadMaxThreshold{0};
        int32_t taskQueueMaxThreshold{0};
        int32_t threadMaxIdleInterval{0};
        int32_t submitTimeoutMs{0};
    };

    HttpServer(kit_muduo::EventLoop *loop, const InetAddress &addr, const std::string &name, bool isPool = true, TcpServer::Option option = TcpServer::Option::kNoRusePort);

    ~HttpServer() = default;

    void start();

    void stop();
    
    void stopAsync(StopCallBack done = StopCallBack());

    const InetAddress& getBindAddr() const { return _server.getBindAddr(); }

    kit_muduo::EventLoop *getLoop() const { return _server.getLoop(); }

    void setHttpCallback(const HttpCallBack &cb) { _httpCallBack = std::move(cb); }

    void setAuthCallback(AuthCallback cb) { _authCallBack = std::move(cb); }

    void setThreadNum(int32_t nums) { _server.setThreadNum(nums); }

    // 启动前配置 HTTP 业务线程池，便于测试和按服务负载调整容量。
    void setBusinessThreadPoolConfig(const BusinessThreadPoolConfig &config);

    std::shared_ptr<HttpServletDispatch> getServletDispatch() { return _dispatch; }

    TcpConnectionPtr getConnection(const std::string &name) { return _server.getConnection(name); }


    RouteResult addRoute(MethodMask methods, const std::string &url, HttpServlet::Ptr svl);

    RouteResult addRoute(const HttpRequest::Method method, const std::string &url, const FunctionServlet::CallBack &cb);
    
    bool Get(const std::string &url, HttpServlet::Ptr svl);
    bool Get(const std::string &url, const FunctionServlet::CallBack &cb);

    bool Post(const std::string &url, HttpServlet::Ptr svl);
    bool Post(const std::string &url, const FunctionServlet::CallBack &cb);

    bool GetAndPost(const std::string &url, HttpServlet::Ptr svl);
    bool GetAndPost(const std::string &url, const FunctionServlet::CallBack &cb);
    
    bool Delete(const std::string &url, HttpServlet::Ptr svl);
    bool Delete(const std::string &url, const FunctionServlet::CallBack &cb);

    bool Ws(const std::string &url, WsPrepareCb cb);

    // ---- 删 ----
    bool removeRoute(uint64_t route_id);
    size_t removeRoute(const std::string &pattern, MethodMask methods);
    size_t removeRoute(const std::string &pattern);

    // ---- 查 ----
    RouteInfo getRoute(uint64_t route_id) const;
    std::vector<RouteInfo> listRoutes() const;
    std::vector<RouteInfo> listRoutes(const std::string &pattern) const;


private:
    void onConnect(TcpConnectionPtr conn);
    void onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receiveTime);

    // http服务器默认处理函数
    void handleRequest(TcpConnectionPtr conn, HttpContextPtr ctx);

    void sendResponse(TcpConnectionPtr conn, HttpContextPtr ctx, bool close_after_send);

private:
    TcpServer _server;
    WebSocketServerPtr _ws_server;
    HttpCallBack _httpCallBack;
    AuthCallback _authCallBack;
    ThreadPool _businessThreadPool;// 注意: 这个是http业务额外的线程池，和处理网络连接evnet_loop的线程池侧重点不一样
    std::shared_ptr<HttpServletDispatch> _dispatch;
    bool _isPool;   // 是否使用线程池
    BusinessThreadPoolConfig _businessThreadPoolConfig;
};



}

#endif
