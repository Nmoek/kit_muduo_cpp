/**
 * @file http_servlet.h
 * @brief HTTP服务分发
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-10 11:51:23
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_SERVLET_H__
#define __KIT_HTTP_SERVLET_H__

#include "net/call_backs.h"
#include "net/http/http_router.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <shared_mutex>

namespace kit_muduo {
namespace http {

enum EXPECTED_HTTP_METHOD {
    HTTP_METHOD_NULL    = 0x00,
    HTTP_METHOD_GET     = 0x01,
    HTTP_METHOD_POST    = 0x02,
    HTTP_METHOD_PUT     = 0x04,
    HTTP_METHOD_DELETE  = 0x08,
};

class HttpServlet
{
public:

    using Ptr = std::shared_ptr<HttpServlet>;

    HttpServlet(const std::string &name)
        :_name(name)
        ,_expectedMethod(0)
    { }

    virtual ~HttpServlet() = default;

    virtual void handle(TcpConnectionPtr conn, HttpContextPtr ctx) = 0;

    int32_t expectedMethod() const { return _expectedMethod; }
    void addExpectedMethod(int32_t method) { _expectedMethod = method; }

    void setName(const std::string &name) { _name = name; }
    std::string name() const { return _name; }

    void setPattern(const std::string &pattern) { _pattern = pattern; }
    std::string pattern() const { return _pattern; }

private:
    /// @brief 服务名称
    std::string _name;
    /// @brief 期望的请求方法
    int32_t _expectedMethod;
    /// @brief 原始url匹配模版
    std::string _pattern;
};



/**
 * @brief 自定义函数类型 服务
 */
class FunctionServlet: public HttpServlet
{
public:
    using CallBack = std::function<void(TcpConnectionPtr , HttpContextPtr)>;

    FunctionServlet(const CallBack &callbcak, const std::string &name = "FunctionServlet");

    ~FunctionServlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    CallBack _callback;
};

class HelloServlet: public HttpServlet
{
public:
    explicit HelloServlet(const std::string &serverName = "kit_proxy_server", const std::string &name = "HelloServlet");

    ~HelloServlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    /// @brief 服务所在服务器名称
    std::string _serverName;
};

class NotFound404Servlet: public HttpServlet
{
public:
    NotFound404Servlet(const std::string &serverName = "kit_proxy_server", const std::string &name = "NotFound404Servlet");

    ~NotFound404Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    /// @brief 服务所在服务器名称
    std::string _serverName;
};

class BadRequest400Servlet: public HttpServlet
{
public:
    BadRequest400Servlet(const std::string &serverName = "kit_proxy_server", const std::string &name = "BadRequest400Servlet");

    ~BadRequest400Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    /// @brief 服务所在服务器名称
    std::string _serverName;
};

class ServerErr500Servlet: public HttpServlet
{
public:
    ServerErr500Servlet(const std::string &serverName = "kit_proxy_server", const std::string &name = "ServerErr500Servlet");

    ~ServerErr500Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    /// @brief 服务所在服务器名称
    std::string _serverName;
};

/**
 * @brief 静态文件服务类（获取静态资源等文件）
 */
class StaticFileServlet: public HttpServlet
{
public:
    StaticFileServlet(const std::string &serverName = "kit_proxy_server", const std::string &name = "FileServlet");

    ~StaticFileServlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    /// @brief 服务所在服务器名称
    std::string _serverName;
};


/**
 * @brief 服务分发类
 */
class HttpServletDispatch
{
public:
    HttpServletDispatch();
    ~HttpServletDispatch() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx);

    void addServlet(const std::string &url, HttpServlet::Ptr svl);

    void addServlet(const std::string &url, const FunctionServlet::CallBack &cb);

    void delServlet(const std::string &url, int32_t method);

    void delAllServlet(const std::string &url);

    HttpServlet::Ptr getServlet(const std::string &url, int32_t method);

    std::vector<HttpServlet::Ptr> getAllServlet(const std::string &url);

    void addDynamicServlet(RouterMatcher::Ptr mathcer, HttpServlet::Ptr svl);

    void addDynamicServlet(RouterMatcher::Ptr mathcer, const FunctionServlet::CallBack &cb);

    void delDynamicServlet(const std::string &pattern_url, int32_t method);

    std::pair<RouterMatcher::Ptr, HttpServlet::Ptr>& getDynamicServlet(const std::string &pattern_url, int32_t method);

    HttpServlet::Ptr RouterMatch(HttpContextPtr ctx);

private:
    // 设计目的: 配合RESTful风格 一个路径 不同方法 对应到不同的服务上
    using ServletMap = std::unordered_multimap<std::string, HttpServlet::Ptr>;
    using ServletVec = std::vector<std::pair<RouterMatcher::Ptr, HttpServlet::Ptr>>;

    HttpServlet::Ptr _defaultSvl;
    /// @brief 精准匹配 /user/1
    ServletMap _servlets;
    // 注意这里必须分两个容器  
    /// @brief 动态匹配 + 模糊匹配  /user/:id 或者 /html/*.html
    ServletVec _dynamicServlets;

    // 读写锁  url路径查询 是显然的读多写少场景 用这个锁有BUG
    // std::shared_timed_mutex _sharedMtx;
    std::mutex _mapMtx;
    std::mutex _vecMtx;

};



}
}   //kit_muduo


#endif