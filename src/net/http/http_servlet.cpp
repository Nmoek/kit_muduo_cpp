/**
 * @file http_servlet.cpp
 * @brief HTTP服务分发
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-10 15:33:03
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_servlet.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_context.h"
#include "net/http/http_util.h"
#include "net/net_log.h"
#include "net/tcp_connection.h"
#include "net/http/http_router.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace kit_muduo::http {

namespace {

std::string RouteKindName(RouteKind kind)
{
    switch(kind)
    {
        case RouteKind::Exact: return "exact";
        case RouteKind::Regex: return "regex";
        case RouteKind::Glob:  return "glob";
        default: return "unknown";
    }
}

bool IsDynamicParamStart(const std::string &pattern, size_t pos)
{
    return pattern[pos] == ':'
        && (pos == 0 || pattern[pos - 1] == '/')
        && pos + 1 < pattern.size()
        && pattern[pos + 1] != '/';
}

bool ContainsDynamicParam(const std::string &pattern)
{
    for(size_t i = 0; i < pattern.size(); ++i)
    {
        if(IsDynamicParamStart(pattern, i))
        {
            return true;
        }
    }
    return false;
}

bool SegmentHasGlobSyntax(const std::string &segment)
{
    return !segment.empty() && segment.find_first_of("*?[") == 0;
}

bool ContainsGlobSegment(const std::string &pattern)
{
    size_t start = 0;
    while(start <= pattern.size())
    {
        const size_t slash = pattern.find('/', start);
        const size_t end = slash == std::string::npos ? pattern.size() : slash;
        if(SegmentHasGlobSyntax(pattern.substr(start, end - start)))
        {
            return true;
        }
        if(slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }
    return false;
}

} // namespace

MethodMask ToMethodMask(HttpRequest::Method method)
{
    switch(method.toInt())
    {
        case HttpRequest::Method::kGet: return ExpectHttpMethods::Get;
        case HttpRequest::Method::kPost: return ExpectHttpMethods::Post;
        case HttpRequest::Method::kHead: return ExpectHttpMethods::Head;
        case HttpRequest::Method::kPut: return ExpectHttpMethods::Put;
        case HttpRequest::Method::kDelete: return ExpectHttpMethods::Delete;
        default: return ExpectHttpMethods::None;
    }
}

bool MethodAllowed(MethodMask expected, HttpRequest::Method actual)
{
    MethodMask actual_mask = ToMethodMask(actual);
    return actual_mask != ExpectHttpMethods::None && (expected & actual_mask) != 0;
}

std::string BuildAllowHeader(MethodMask methods)
{
    std::string allow;
    auto append = [&allow](const char *method) {
        if(!allow.empty())
        {
            allow += ", ";
        }
        allow += method;
    };

    if(methods & ExpectHttpMethods::Get)
    {
        append("GET");
    }
    if(methods & ExpectHttpMethods::Post)
    {
        append("POST");
    }
    if(methods & ExpectHttpMethods::Head)
    {
        append("HEAD");
    }
    if(methods & ExpectHttpMethods::Put)
    {
        append("PUT");
    }
    if(methods & ExpectHttpMethods::Delete)
    {
        append("DELETE");
    }

    return allow;
}

HttpServlet::HttpServlet(const std::string &name, const std::string &server_name)
    :_name(name)
    ,_server_name(server_name)
{ }

/***********FunctionServlet************ */

FunctionServlet::FunctionServlet(const CallBack &callbcak, const std::string &name, const std::string &server_name)
    :HttpServlet(name, server_name)
    ,_callback(callbcak)
{

}

void FunctionServlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    _callback(conn, ctx);
}

/***********FunctionServlet************ */

/***********HelloServlet************ */
HelloServlet::HelloServlet(const std::string &server_name, const std::string &name)
    :HttpServlet(name, server_name)
{

}


void HelloServlet::Handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->addHeader("Content-Type", "text/html");

    std::string body =
"<html>"
"<head><title>Hello, Welcome my server!</title></head>"
"<body>"
  "<div style=\"text-align:center\">"
    "<h1>Hello, Welcome my server!</h1>"
    "<p>kit server</p>"
  "</div>"
"</body>"
"</html>";

    resp->body().appendData(body);
}

void HelloServlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    Handle(conn, ctx);
}

/***********HelloServlet************ */

/**********NotFound404Servlet********** */

NotFound404Servlet::NotFound404Servlet()
    :HttpServlet("NotFound404Servlet", "kit_server")
{

}


void NotFound404Servlet::Handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k404NotFound);
    resp->setConnectionClosed(true);
    resp->addHeader("Content-Type", "text/html");

    std::string body =
"<html>"
"<head><title>404 Not Found</title></head>"
"<body>"
  "<div style=\"text-align:center\">"
    "<h1>404 Not Found</h1>"
    "<p> kit_server </p>"
  "</div>"
"</body>"
"</html>";

    resp->body().appendData(body);
}

void NotFound404Servlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    Handle(conn, ctx);
}
/**********NotFound404Servlet********** */

BadRequest400Servlet::BadRequest400Servlet()
    :HttpServlet("BadRequest400Servlet", "kit_server")
{

}

void BadRequest400Servlet::Handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();

    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k400BadRequest);
    resp->setConnectionClosed(true);
    resp->body().reset();
}

void BadRequest400Servlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    Handle(conn, ctx);
}


ServerErr500Servlet::ServerErr500Servlet()
    :HttpServlet("ServerErr500Servlet", "kit_server")
{

}

void ServerErr500Servlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();

    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k500InternalServerError);
    resp->setConnectionClosed(true);
    resp->addHeader("Content-Type", "text/html");

    std::string body =
"<html>"
"<head><title>500 Server Error</title></head>"
"<body>"
  "<div style=\"text-align:center\">"
    "<h1>500 Server Error</h1>"
    "<p>kit server</p>"
  "</div>"
"</body>"
"</html>";

    resp->body().appendData(body);
}

StaticFileServlet::StaticFileServlet()
    :HttpServlet("FileServlet", "kit_server")
{}

static int32_t GetStaticType(const std::string &suffix_type)
{
    if(suffix_type == "html")
        return ContentType::kHtml;
    else if(suffix_type == "jpg" || suffix_type == "jpeg")
        return ContentType::kImageJpgType;
    else if(suffix_type == "css")
        return ContentType::kCss;
    else if(suffix_type == "js")
        return ContentType::kJavaScript;
    return ContentType::kJsonType;
}

void StaticFileServlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();
    auto req = ctx->request();

    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    const std::string &path = req->path();
    // 文件名全称
    auto pos = path.find_last_of("/");
    std::string file_name = path.substr(pos + 1);
    // 文件类型
    pos = file_name.find_last_of(".");
    const std::string& suffix_type = file_name.substr(pos + 1);

    resp->body().setContentType(GetStaticType(suffix_type));

    const std::string target_path = "web/" + suffix_type + "/" + file_name;

    /// TODO: 可使用sendfile优化 减少拷贝
    std::fstream tmp_f(target_path, std::ios::in | std::ios::binary);
    if(!tmp_f.is_open())
    {
        HTTP_F_ERROR("file %s open error! %d:%s \n", target_path.c_str(), errno, strerror(errno));
        resp->setStateCode(StateCode::k500InternalServerError);
        return;
    }
    std::string data;
    uint64_t file_size = 0;
    tmp_f.seekg(0, std::ios::end);
    file_size = tmp_f.tellg();
    tmp_f.seekg(0, std::ios::beg);
    data.resize(file_size);

    tmp_f.read((char*)data.data(), file_size);
    resp->body().appendData(data);

}

/***********ServletDispatch************ */

HttpServletDispatch::HttpServletDispatch()
    :_defaultSvl(std::make_shared<NotFound404Servlet>())
{

}


void HttpServletDispatch::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    MatchResult result = match(ctx);

    if(result.status == MatchStatus::Found && result.servlet)
    {
        HTTP_F_DEBUG("conn[%s], path[%s] HttpServlet[%s] handling...... \n", conn->name().c_str(), req->path().c_str(), result.servlet->name().c_str());
        
        result.servlet->handle(conn, ctx);
        return;
    }


    if(result.status == MatchStatus::PathFoundMethodNotAllowed)
    {
        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);

        resp->setStateCode(StateCode::k405MethodNotAllowed);
        resp->addHeader("Allow", BuildAllowHeader(result.allowed_methods));
        resp->addHeader("Content-Length", "0");

        HTTP_F_WARN("http method not allowed! path[%s], method[%s], allow[%s]\n", req->path().c_str(), req->method().toString(), BuildAllowHeader(result.allowed_methods).c_str());
        
        return;
    }

    HTTP_DEBUG() << req->path() << " default svl handle!" << std::endl;
    _defaultSvl->handle(conn, ctx);
}

RouteResult HttpServletDispatch::addRoute(MethodMask methods, const std::string &pattern, HttpServlet::Ptr servlet)
{
    RouteResult result;
    if(pattern.empty())
    {
        result.status = RouteStatus::InvalidArgument;
        result.message = "route pattern is empty";
        HTTP_F_ERROR("addRoute failed: pattern is empty\n");
        return result;
    }
    if(!servlet)
    {
        result.status = RouteStatus::InvalidArgument;
        result.message = "route servlet is null";
        HTTP_F_ERROR("addRoute failed: servlet is null, pattern[%s]\n", pattern.c_str());
        return result;
    }
    if((methods & ExpectHttpMethods::All) == ExpectHttpMethods::None || (methods & ~ExpectHttpMethods::All) != 0)
    {
        result.status = RouteStatus::InvalidArgument;
        result.message = "route method mask is invalid";
        HTTP_F_ERROR("addRoute failed: invalid method mask[0x%x], pattern[%s]\n", methods, pattern.c_str());
        return result;
    }

    RouteKind kind = routeKind(pattern);
    RouterMatcher::Ptr matcher = createMatcher(kind, pattern);
    if(kind != RouteKind::Exact && !matcher)
    {
        result.status = RouteStatus::InvalidArgument;
        result.message = "create route matcher failed";
        HTTP_F_ERROR("addRoute failed: create matcher failed, pattern[%s]\n", pattern.c_str());
        return result;
    }

    std::unique_lock<std::mutex> lock(route_mtx_);
    MethodMask conflict_methods = ExpectHttpMethods::None;

    if(kind == RouteKind::Exact)
    {
        auto &routes = exact_routes_[pattern];
        if(hasMethodConflict(routes, methods, &conflict_methods))
        {
            result.status = RouteStatus::Conflict;
            result.message = "route method conflict";
            HTTP_F_ERROR("addRoute conflict: kind[%s], pattern[%s], new_methods[%s], conflict_methods[%s]\n",
                         RouteKindName(kind).c_str(), pattern.c_str(), BuildAllowHeader(methods).c_str(), BuildAllowHeader(conflict_methods).c_str());
            return result;
        }

        RouteEntry entry;
        entry.id = next_route_id_++;
        entry.kind = kind;
        entry.pattern = pattern;
        entry.methods = methods;
        entry.servlet = std::move(servlet);
        entry.priority = next_priority_++;
        routes.emplace_back(std::move(entry));
        result.route_id = routes.back().id;
    }
    else
    {
        std::vector<RouteEntry> same_pattern_routes;
        for(const auto &route : dynamic_routes_)
        {
            if(route.pattern == pattern)
            {
                same_pattern_routes.emplace_back(route);
            }
        }
        if(hasMethodConflict(same_pattern_routes, methods, &conflict_methods))
        {
            result.status = RouteStatus::Conflict;
            result.message = "route method conflict";
            HTTP_F_ERROR("addRoute conflict: kind[%s], pattern[%s], new_methods[%s], conflict_methods[%s]\n",
                         RouteKindName(kind).c_str(), pattern.c_str(), BuildAllowHeader(methods).c_str(), BuildAllowHeader(conflict_methods).c_str());
            return result;
        }

        RouteEntry entry;
        entry.id = next_route_id_++;
        entry.kind = kind;
        entry.pattern = pattern;
        entry.methods = methods;
        entry.matcher = std::move(matcher);
        entry.servlet = std::move(servlet);
        entry.priority = next_priority_++;
        dynamic_routes_.emplace_back(std::move(entry));
        result.route_id = dynamic_routes_.back().id;
    }

    HTTP_F_INFO("addRoute success: id[%llu], kind[%s], pattern[%s], methods[%s]\n",
                static_cast<unsigned long long>(result.route_id),
                RouteKindName(kind).c_str(),
                pattern.c_str(),
                BuildAllowHeader(methods).c_str());
    return result;
}

RouteResult HttpServletDispatch::addRoute(MethodMask methods, const std::string &pattern, const FunctionServlet::CallBack &cb)
{
    return addRoute(methods, pattern, std::make_shared<FunctionServlet>(cb));
}

RouteKind HttpServletDispatch::routeKind(const std::string &pattern) const
{
    if(ContainsDynamicParam(pattern))
    {
        return RouteKind::Regex;
    }
    if(ContainsGlobSegment(pattern))
    {
        return RouteKind::Glob;
    }
    return RouteKind::Exact;
}

RouterMatcher::Ptr HttpServletDispatch::createMatcher(RouteKind kind, const std::string &pattern) const
{
    try
    {
        switch(kind)
        {
            case RouteKind::Regex:
                return std::make_shared<RegexRouterMatcher>(pattern);
            case RouteKind::Glob:
                return std::make_shared<GlobRouterMatcher>(pattern);
            case RouteKind::Exact:
                return nullptr;
            default:
                return nullptr;
        }
    }
    catch(const std::exception &e)
    {
        HTTP_F_ERROR("create matcher failed! %s, pattern:%s\n", e.what(), pattern.c_str());
        return nullptr;
    }
}

HttpServletDispatch::MatchResult HttpServletDispatch::match(HttpContextPtr ctx)
{
    MatchResult result;
    auto req = ctx->request();
    const std::string url = req->path();

    std::unique_lock<std::mutex> lock(route_mtx_);

    auto exact_it = exact_routes_.find(url);
    if(exact_it != exact_routes_.end())
    {
        for(const auto &route : exact_it->second)
        {
            result.allowed_methods |= route.methods;
            if(MethodAllowed(route.methods, req->method()))
            {
                result.status = MatchStatus::Found;
                result.servlet = route.servlet;
                result.allowed_methods = route.methods;
                return result;
            }
        }

        result.status = MatchStatus::PathFoundMethodNotAllowed;
        return result;
    }

    for(auto &route : dynamic_routes_)
    {
        if(!route.matcher || !route.servlet)
        {
            HTTP_F_WARN("route id[%llu] matcher or servlet is null! \n", static_cast<unsigned long long>(route.id));
            continue;
        }
        if(!route.matcher->MatchPath(url))
        {
            continue;
        }

        result.allowed_methods |= route.methods;
        if(MethodAllowed(route.methods, req->method()))
        {
            route.matcher->Match(ctx);
            result.status = MatchStatus::Found;
            result.servlet = route.servlet;
            result.allowed_methods = route.methods;
            return result;
        }
    }

    if(result.allowed_methods != ExpectHttpMethods::None)
    {
        result.status = MatchStatus::PathFoundMethodNotAllowed;
    }

    return result;
}

bool HttpServletDispatch::hasMethodConflict(const std::vector<RouteEntry> &routes, MethodMask methods, MethodMask *conflict_methods) const
{
    MethodMask conflict = ExpectHttpMethods::None;
    for(const auto &route : routes)
    {
        conflict |= (route.methods & methods);
    }

    if(conflict_methods)
    {
        *conflict_methods = conflict;
    }

    return conflict != ExpectHttpMethods::None;
}

/***********ServletDispatch************ */


}
   //kit_muduo
