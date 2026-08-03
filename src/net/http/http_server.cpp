/**
 * @file http_server.cpp
 * @brief HTTP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 20:03:17
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_server.h"
#include "net/call_backs.h"
#include "net/http/http_context.h"
#include "net/http/http_parser.h"
#include "net/http/http_servlet.h"
#include "net/http/http_util.h"
#include "net/net_log.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/websocket/websocket_server.h"
#include <exception>
#include <memory>
#include <sstream>


namespace kit_muduo {
namespace http {


HttpServer::HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, bool isPool, TcpServer::Option option)
    :_server(loop, addr, name, option)
    ,_ws_server(std::make_shared<ws::WebSocketServer>())
    ,_httpCallBack(nullptr)
    ,_authCallBack(nullptr)
    ,_dispatch(std::make_shared<HttpServletDispatch>())
    ,_isPool(isPool)
    ,_businessThreadPoolConfig{
        static_cast<int32_t>(3 * std::thread::hardware_concurrency()),
        static_cast<int32_t>(30 * std::thread::hardware_concurrency()),
        2,
        300
    }
{
    _server.setConnectionCallback(std::bind(&HttpServer::onConnect, this, std::placeholders::_1));

    _server.setMessageCallback(std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    setHttpCallback(std::bind(&HttpServer::handleRequest, this, std::placeholders::_1, std::placeholders::_2));
}

void HttpServer::start()
{
    if(_isPool)
    {
        _businessThreadPool.setMode(ThreadPool::CACHE_MOD);
        _businessThreadPool.setThreadMaxThreshHold(_businessThreadPoolConfig.max_threads);
        _businessThreadPool.setTaskQueMaxThreshHold(_businessThreadPoolConfig.max_task_queue);
        _businessThreadPool.setThreadMaxIdleInterval(_businessThreadPoolConfig.thread_idle_seconds);
        _businessThreadPool.start();
    }

    _server.start();
}

void HttpServer::stop()
{
    stopAsync();
}

void HttpServer::stopAsync(StopCallBack done)
{
    if(_isPool)
    {
        _businessThreadPool.stop();
    }
    _server.stopAsync(std::move(done));
}

void HttpServer::setBusinessThreadPoolConfig(const BusinessThreadPoolConfig &config)
{
    _businessThreadPoolConfig = config;
}

RouteResult HttpServer::addRoute(MethodMask methods, const std::string &url, HttpServlet::Ptr svl)
{
    auto result = _dispatch->addRoute(methods, url, std::move(svl));
    if(!result.ok())
    {
        HTTP_F_ERROR("HttpServer addRoute failed: url[%s], methods[%s], reason[%s]\n",
                     url.c_str(), BuildAllowHeader(methods).c_str(), result.message.c_str());
    }
    return result;
}

RouteResult HttpServer::addRoute(const HttpRequest::Method method, const std::string &url, const FunctionServlet::CallBack &cb)
{
    return addRoute(ToMethodMask(method), url, std::make_shared<FunctionServlet>(cb));
}

bool HttpServer::Get(const std::string &url, HttpServlet::Ptr svl)
{
    auto res = addRoute(ExpectHttpMethods::Get, url, std::move(svl));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;
}

bool HttpServer::Get(const std::string &url, const FunctionServlet::CallBack &cb)
{
    auto res = addRoute(ExpectHttpMethods::Get, url, std::make_shared<FunctionServlet>(cb));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;
}
bool HttpServer::Post(const std::string &url, HttpServlet::Ptr svl)
{
    auto res = addRoute(ExpectHttpMethods::Post, url, std::move(svl));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;
}

bool HttpServer::Post(const std::string &url, const FunctionServlet::CallBack &cb)
{
    auto res = addRoute(ExpectHttpMethods::Post, url, std::make_shared<FunctionServlet>(cb));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;
}

bool HttpServer::GetAndPost(const std::string &url, HttpServlet::Ptr svl)
{
    auto res = addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, url, std::move(svl));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;}

bool HttpServer::GetAndPost(const std::string &url, const FunctionServlet::CallBack &cb)
{
    auto res = addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, url, std::make_shared<FunctionServlet>(cb));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;
}


bool HttpServer::Delete(const std::string &url, HttpServlet::Ptr svl)
{
    auto res = addRoute(ExpectHttpMethods::Delete, url, std::move(svl));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }
    
    return true;}

bool HttpServer::Delete(const std::string &url, const FunctionServlet::CallBack &cb)
{
    auto res = addRoute(ExpectHttpMethods::Delete, url, std::make_shared<FunctionServlet>(cb));
    if(!res.ok())
    {
        HTTP_F_ERROR("addRoute error! %s \n", res.message.c_str());
        return false;
    }

    return true;
}

bool HttpServer::Ws(const std::string &url, WsPrepareCb cb)
{
    // 路由路径
    return Get(url, [this, cb](auto &&arg1, auto &&arg2){
        _ws_server->handleUpgrade(
            std::forward<decltype(arg1)>(arg1), 
            std::forward<decltype(arg2)>(arg2),
            cb
        );
    });
}

bool HttpServer::removeRoute(uint64_t route_id)
{
    return _dispatch->removeRoute(route_id);
}

size_t HttpServer::removeRoute(const std::string &pattern, MethodMask methods)
{
    return _dispatch->removeRoute(pattern, methods);
}

size_t HttpServer::removeRoute(const std::string &pattern)
{
    return _dispatch->removeRoute(pattern);
}

RouteInfo HttpServer::getRoute(uint64_t route_id) const
{
    return _dispatch->getRoute(route_id);
}

std::vector<RouteInfo> HttpServer::listRoutes() const
{
    return _dispatch->listRoutes();
}

std::vector<RouteInfo> HttpServer::listRoutes(const std::string &pattern) const
{
    return _dispatch->listRoutes(pattern);
}

void HttpServer::onConnect(TcpConnectionPtr conn)
{
    if(conn->connected())
    {
        HTTP_F_INFO("==> new connection fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());

        conn->setContext(std::make_shared<HttpContext>());
    }
    else
    {
        HTTP_F_INFO("==> disconnected connection  fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());
    }
}

inline void CheckHttpParseError(HttpContextPtr ctx)
{
    switch (ctx->parseError()) 
    {
        // 414 URI Too Long
        case HttpParseError::kStartLineTooLarge:
            URITooLong414Servlet::Handle(nullptr, ctx);
            break;
        // 431 Request Header Fields Too Large
        case HttpParseError::kHeadersTooLarge:
        case HttpParseError::kHeadersTooMany:
            RequestHeaderFieldsTooLarge431Servlet::Handle(nullptr, ctx);
            break;
        // 413 Payload Too Large
        case HttpParseError::kBodyTooLarge:
            PayloadTooLarge413Servlet::Handle(nullptr, ctx);
            break;
        default:
            BadRequest400Servlet::Handle(nullptr, ctx);
    }
}

void HttpServer::onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receiveTime)
{
    bool is_exception = false;
    std::shared_ptr<HttpContext> context = std::static_pointer_cast<HttpContext>(conn->getContext());
    if(nullptr == context)
    {
        HTTP_ERROR() << "http context is null!" << std::endl;
        return;
    }
 
    while(buf->readableBytes() > 0)
    {
        size_t before_len = buf->readableBytes();
        auto req = context->request();
        auto resp = context->response();

        if(!context->parseRequest(*buf, receiveTime))
        {
            HTTP_F_ERROR("http request parse error!\n");
            CheckHttpParseError(context);

            (void)sendResponse(conn, context, true);
            return;
        }

        // 未解析完 等待更多数据
        if(!context->gotAll())
        {
            HTTP_F_DEBUG("http data not complete! %lu --> %lu \n", before_len, buf->readableBytes());
            break;
        }
        // TODO 未来middlewire责任链处理都放在这里
        // 权限校验
        if(_authCallBack)
        {
            AuthCheckResult auth_result = _authCallBack(context);
            if(!auth_result.ok)
            {
                resp->setVersion(Version::kHttp11);
                resp->setStateCode(auth_result.redirect_to_login ? StateCode::k302MoveTemporarily : auth_result.http_status);
                if(auth_result.redirect_to_login)
                {
                    resp->addHeader("Location", "/html/login.html");
                    resp->setConnectionClosed(true);
                }
                else
                {
                    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));
                    resp->appendBodyData(auth_result.message);
                }

                HTTP_F_INFO("authentication fail [%d][%s] ===> %s \n", conn->fd(), conn->name().c_str(), req->path().c_str());

                sendResponse(conn, context, resp->connectionClosed());

                // 重置conn中的上下文
                context = std::make_shared<HttpContext>();
                conn->setContext(context);
                continue;
            }
        }

        try {
            // 判断是否满足升级要素
            if(context->maybeUpgrade())
            {
                auto outcome = _dispatch->handleWithOutcome(conn, context, false);
                if(MatchStatus::kFound != outcome.status || nullptr == outcome.servlet)
                {
                    NotFound404Servlet::Handle(conn, context);
                    resp->resetBodyData();

                    sendResponse(conn, context, true);
                    return;
                }

                outcome.servlet->handle(conn, context);

                sendResponse(conn, context, resp->connectionClosed());
                
                // 链路升级成功
                if(StateCode::k101SwitchingProtocols == resp->stateCode().toInt())
                {
                    HTTP_F_DEBUG("http upgrade succes\n");
                    // 执行websocket session打开成功的回调
                    _ws_server->onOpen(conn);
                    // 兜底处理剩余字节
                    _ws_server->drainRemainingWebSocketBytes(conn, buf, receiveTime);
                    // 注意必须返回, websocket session 接管生命周期
                    return;
                }
            }
            else
            {
                _httpCallBack(conn, context);
            }


        } catch(const std::exception &e) {

            HTTP_F_ERROR("http callback exception: %s \n", e.what());

            is_exception = true;
        } catch(...) {

            HTTP_F_ERROR("http callback unknown exception\n");

            is_exception = true;
        }

        if(is_exception)
        {
            ServerErr500Servlet::Handle(conn, context);
            context->response()->setConnectionClosed(true);
            context->response()->resetBodyData();

            sendResponse(conn, context, true);
            return;
        }
        // 重置conn中的上下文
        context = std::make_shared<HttpContext>();
        conn->setContext(context);

    }


}

void HttpServer::handleRequest(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto work_func = [this](TcpConnectionPtr conn, HttpContextPtr ctx, std::shared_ptr<HttpServletDispatch> dispatch) {

        auto req = ctx->request();
        auto resp = ctx->response();

        HTTP_F_INFO("woker thread [%d][%s] ===> %s \n", conn->fd(), conn->name().c_str(), req->path().c_str());

        const std::string &connection = req->getHeader("Connection");

        bool closed = HeaderContainsToken(connection, "close")
                || (Version::kHttp10 == req->version().toInt() && !HeaderContainsToken(connection, "keep-alive"));
        resp->setConnectionClosed(closed);

        dispatch->handle(conn, ctx);
    
        sendResponse(conn, ctx, resp->connectionClosed());
        return;
    };

    if(_isPool)
    {
        auto submit_result = _businessThreadPool.trySubmitTask(_businessThreadPoolConfig.submit_timeout_ms, work_func, conn, ctx, _dispatch);

        if(!submit_result.ok())
        {
            HTTP_F_WARN("submit task error! fd[%d][%s], path[%s] \n", conn->fd(), conn->name().c_str(), ctx->request()->path().c_str());

            ServiceUnavailable503Servlet::Handle(conn, ctx);
            return sendResponse(conn, ctx, true);
        }

    }
    else
    {
        work_func(conn, ctx, _dispatch);
    }

}

void HttpServer::sendResponse(TcpConnectionPtr conn, HttpContextPtr ctx, bool close_after_send)
{
    auto resp = ctx->response();
    auto response_bytes = std::make_shared<std::vector<uint8_t>>(resp->toBytes());

    conn->send(*response_bytes);
    if(close_after_send)
    {
        conn->shutdown();
        return;
    }

    return;
}

}
}
