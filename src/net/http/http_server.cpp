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
#include "net/http/http_servlet.h"
#include "net/http/http_util.h"
#include "net/net_log.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "base/content_parser.h"

namespace kit_muduo {
namespace http {


HttpServer::HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, bool isPool, TcpServer::Option option)
    :_server(loop, addr, name, option)
    ,_httpCallBack(nullptr)
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
        _businessThreadPool.setThreadMaxThreshHold(_businessThreadPoolConfig.threadMaxThreshold);
        _businessThreadPool.setTaskQueMaxThreshHold(_businessThreadPoolConfig.taskQueueMaxThreshold);
        _businessThreadPool.setThreadMaxIdleInterval(_businessThreadPoolConfig.threadMaxIdleInterval);
        _businessThreadPool.start();
    }

    _server.start();
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
    
    return true;}

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

void HttpServer::onMessage(TcpConnectionPtr conn, Buffer *buf, TimeStamp receiveTime)
{
    std::shared_ptr<HttpContext> context = std::static_pointer_cast<HttpContext>(conn->getContext());
    if(nullptr == context)
    {
        HTTP_ERROR() << "http context is null!" << std::endl;
        return;
    }

    // 这里使用HttpContext的目的是分段解析Http报文
    if(!context->parseRequest(*buf, receiveTime))
    {
        HTTP_ERROR() << "http request parse error! " << std::endl;
        BadRequest400Servlet svl;

        svl.handle(conn, context);
        conn->send(context->response()->toString());
        conn->shutdown();

        context.reset(new HttpContext());
    }

    if(context->gotAll())
    {
        // TODO： 需要对请求的方法等进行校验
        // 1. 检查请求方法
        // 2. TODO检查其他什么要素?

        std::shared_ptr<HttpContext> ctx2;
        ctx2.swap(context);
        // 重置conn中的上下文
        conn->setContext(std::make_shared<HttpContext>());
        _httpCallBack(conn, ctx2);
    }

}


#if 1
void HttpServer::handleRequest(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto work_func = [](TcpConnectionPtr conn, HttpContextPtr ctx, std::shared_ptr<HttpServletDispatch> dispatch) {

        auto req_ptr = ctx->request();
        auto resp_ptr = ctx->response();

        HTTP_F_INFO("woker thread [%d]][%s] ===> %s \n", conn->fd(), conn->name().c_str(), req_ptr->path().c_str());

        const std::string &connection = req_ptr->getHeader("Connection");
        bool closed = (connection == "close")
                || (Version::kHttp10 == req_ptr->version()() && connection != "keep-alive");
        resp_ptr->setConnectionClosed(closed);

        // TODO 中间层检验
        // 1. 身份鉴权
        // if(身份鉴权 == 1)
        // {
            // 实际业务分发
            dispatch->handle(conn, ctx);

        // }

        // TODO 这里都要改 send 接口不应该是string
        conn->send(resp_ptr->toString());
        if(resp_ptr->connectionClosed())
        {
            conn->shutdown();
        }

    };

    if(_isPool)
    {
        auto submit_result = _businessThreadPool.trySubmitTask(_businessThreadPoolConfig.submitTimeoutMs, work_func, conn, ctx, _dispatch);

        if(!submit_result.ok())
        {
            HTTP_F_WARN("submit task error! fd[%d][%s], path[%s] \n", conn->fd(), conn->name().c_str(), ctx->request()->path().c_str());

            ServiceUnavailable503Servlet::Handle(conn, ctx);
            conn->send(ctx->response()->toString());
            conn->shutdown();
            return;
        }

    }
    else
    {
        work_func(conn, ctx, _dispatch);
    }

}

#else
void HttpServer::handleRequest(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    const std::string &connection = req->getHeader("Connection");
    bool closed = (connection == "close")
            || (Version::kHttp10 == req->version()() && connection != "keep-alive");

    HelloServlet svl;

    resp->setConnectionClosed(closed);

    svl.handle(conn, ctx);

    conn->send(resp->toString());
    if(resp->connectionClosed())
    {
        conn->shutdown();
    }
}
#endif


}
}
