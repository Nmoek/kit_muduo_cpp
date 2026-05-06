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
#include "net/http/http_request.h"
#include "net/http/http_router.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>

namespace kit_muduo {
namespace http {

using MethodMask = uint32_t;

namespace ExpectHttpMethods {
    constexpr MethodMask None   = 0;
    constexpr MethodMask Get    = 1 << 0;
    constexpr MethodMask Post   = 1 << 1;
    constexpr MethodMask Head   = 1 << 2;
    constexpr MethodMask Put    = 1 << 3;
    constexpr MethodMask Delete = 1 << 4;
    constexpr MethodMask All    = Get | Post | Head | Put | Delete;
}

MethodMask ToMethodMask(HttpRequest::Method method);
bool MethodAllowed(MethodMask expected, HttpRequest::Method actual);
std::string BuildAllowHeader(MethodMask methods);

enum class RouteKind {
    Exact,
    Regex,
    Glob,
};

enum class RouteStatus {
    Ok,
    InvalidArgument,
    Conflict,
};

struct RouteResult {
    RouteStatus status{RouteStatus::Ok};
    uint64_t route_id{0};
    std::string message;

    bool ok() const { return status == RouteStatus::Ok; }
};

/// 路由信息快照，供查询接口返回。
struct RouteInfo {
    uint64_t id{0};
    RouteKind kind{RouteKind::Exact};
    std::string pattern;
    MethodMask methods{ExpectHttpMethods::None};
    std::string methods_str;  // 可读形式，如 "GET, POST"
};

enum class MatchStatus {
    Found,
    PathFoundMethodNotAllowed,
    NotFound,
};


class HttpServlet
{
public:

    using Ptr = std::shared_ptr<HttpServlet>;

    explicit HttpServlet(const std::string &name, const std::string &server_name = "kit_server");
    virtual ~HttpServlet() = default;

    virtual void handle(TcpConnectionPtr conn, HttpContextPtr ctx) = 0;

    void setName(const std::string &name) { _name = name; }
    std::string name() const { return _name; }

    void setPattern(const std::string &pattern) { _pattern = pattern; }
    std::string pattern() const { return _pattern; }

protected:
    /// @brief 服务名称
    std::string _name;
    /// @brief 所在服务器名称
    std::string _server_name;
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

    explicit FunctionServlet(const CallBack &callbcak, const std::string &name = "FunctionServlet", const std::string &server_name = "kit_server");

    ~FunctionServlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

private:
    CallBack _callback;
};

class HelloServlet: public HttpServlet
{
public:
    explicit HelloServlet(const std::string &server_name = "kit_server", const std::string &name = "HelloServlet");

    ~HelloServlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

    static void Handle(TcpConnectionPtr conn, HttpContextPtr ctx);

};

class NotFound404Servlet: public HttpServlet
{
public:
    NotFound404Servlet();

    ~NotFound404Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

    static void Handle(TcpConnectionPtr conn, HttpContextPtr ctx);

};

class BadRequest400Servlet: public HttpServlet
{
public:
    BadRequest400Servlet();

    ~BadRequest400Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

    static void Handle(TcpConnectionPtr conn, HttpContextPtr ctx);
};

class MethodNotAllowed405Servlet: public HttpServlet
{
public:
    MethodNotAllowed405Servlet();

    ~MethodNotAllowed405Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

    static void Handle(TcpConnectionPtr conn, HttpContextPtr ctx);
};


class ServerErr500Servlet: public HttpServlet
{
public:
    ServerErr500Servlet();

    ~ServerErr500Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

    static void Handle(TcpConnectionPtr conn, HttpContextPtr ctx);

};

class ServiceUnavailable503Servlet: public HttpServlet
{
public:
    ServiceUnavailable503Servlet();

    ~ServiceUnavailable503Servlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

    static void Handle(TcpConnectionPtr conn, HttpContextPtr ctx);

};

/**
 * @brief 静态文件服务类（获取静态资源等文件）
 */
class StaticFileServlet: public HttpServlet
{
public:
    StaticFileServlet();

    ~StaticFileServlet() = default;

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override;

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

    RouteResult addRoute(MethodMask methods, const std::string &pattern, HttpServlet::Ptr servlet);
    RouteResult addRoute(MethodMask methods, const std::string &pattern, const FunctionServlet::CallBack &cb);

    // ---- 删 ----

    /// 按 route_id 精确删除（addRoute 返回的 RouteResult 中有 id）。
    /// @return true 找到并删除，false id 不存在。
    bool removeRoute(uint64_t route_id);

    /// 按 pattern + method mask 精确匹配删除。
    /// @return 删除的条目数（0 表示无匹配）。
    size_t removeRoute(const std::string &pattern, MethodMask methods);

    /// 删除该 pattern 下所有 method 的条目。
    /// @return 删除的条目数。
    size_t removeRoute(const std::string &pattern);

    // ---- 查 ----

    /// 按 id 查询单条路由。id 不存在时返回 id==0 的 RouteInfo。
    RouteInfo getRoute(uint64_t route_id) const;

    /// 列出所有已注册路由的快照。
    std::vector<RouteInfo> listRoutes() const;

    /// 列出指定 pattern 下所有 method 的路由。
    std::vector<RouteInfo> listRoutes(const std::string &pattern) const;

private:
    struct RouteEntry {
        uint64_t id{0};
        RouteKind kind{RouteKind::Exact};
        std::string pattern;
        MethodMask methods{ExpectHttpMethods::None};
        RouterMatcher::Ptr matcher;
        HttpServlet::Ptr servlet;
        int priority{0};
    };

    struct MatchResult {
        MatchStatus status{MatchStatus::NotFound};
        HttpServlet::Ptr servlet;
        MethodMask allowed_methods{ExpectHttpMethods::None};
    };

    RouteKind routeKind(const std::string &pattern) const;
    RouterMatcher::Ptr createMatcher(RouteKind kind, const std::string &pattern) const;
    MatchResult match(HttpContextPtr ctx);
    bool hasMethodConflict(const std::vector<RouteEntry> &routes, MethodMask methods, MethodMask *conflict_methods) const;

    HttpServlet::Ptr _defaultSvl;
    std::unordered_map<std::string, std::vector<RouteEntry>> exact_routes_;
    std::vector<RouteEntry> dynamic_routes_;
    uint64_t next_route_id_{1};
    int next_priority_{0};
    mutable std::mutex route_mtx_;

};



}
}   //kit_muduo


#endif
