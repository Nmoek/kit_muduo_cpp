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
#include "net/net_log.h"
#include "net/tcp_connection.h"
#include "net/http/http_router.h"


namespace kit_muduo::http {


/***********FunctionServlet************ */

FunctionServlet::FunctionServlet(const CallBack &callbcak, const std::string &name)
    :HttpServlet(name)
    ,_callback(callbcak)
{

}

void FunctionServlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    _callback(conn, ctx);
}

/***********FunctionServlet************ */

/***********HelloServlet************ */
HelloServlet::HelloServlet(const std::string &serverName, const std::string &name)
    :HttpServlet(name)
    ,_serverName(serverName)
{

}


void HelloServlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
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
    "<p>" + _serverName + "</p>"
  "</div>"
"</body>"
"</html>";

    resp->body().appendData(body);
}

/***********HelloServlet************ */

/**********NotFound404Servlet********** */

NotFound404Servlet::NotFound404Servlet(const std::string &serverName, const std::string &name)
    :HttpServlet(name)
    ,_serverName(serverName)
{

}


void NotFound404Servlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
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
    "<p>" + _serverName + "</p>"
  "</div>"
"</body>"
"</html>";

    resp->body().appendData(body);
}

/**********NotFound404Servlet********** */

BadRequest400Servlet::BadRequest400Servlet(const std::string &serverName, const std::string &name)
    :HttpServlet(name)
    ,_serverName(serverName)
{

}

void BadRequest400Servlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();

    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k400BadRequest);
    resp->setConnectionClosed(true);
    resp->body().reset();
}


ServerErr500Servlet::ServerErr500Servlet(const std::string &serverName, const std::string &name)
    :HttpServlet(name)
    ,_serverName(serverName)
{

}

void ServerErr500Servlet::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto resp = ctx->response();

    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k404NotFound);
    resp->setConnectionClosed(true);
    resp->addHeader("Content-Type", "text/html");

    std::string body =
"<html>"
"<head><title>500 Server Error</title></head>"
"<body>"
  "<div style=\"text-align:center\">"
    "<h1>500 Server Error</h1>"
    "<p>" + _serverName + "</p>"
  "</div>"
"</body>"
"</html>";

    resp->body().appendData(body);
}
StaticFileServlet::StaticFileServlet(const std::string &serverName, const std::string &name)
    :HttpServlet(name)
    ,_serverName(serverName)
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
    :_defaultSvl(std::make_shared<HelloServlet>())
{

}

static inline int32_t MethodConver(int32_t method)
{
    switch(method)
    {
        case HttpRequest::Method::kGet: return HTTP_METHOD_GET;
        case HttpRequest::Method::kPost: return HTTP_METHOD_POST;
        case HttpRequest::Method::kPut: return HTTP_METHOD_PUT;
        case HttpRequest::Method::kDelete: return HTTP_METHOD_DELETE;
        default:
            return HTTP_METHOD_NULL;
    }
}

static inline bool CheckMethodValid(int32_t src, int32_t tar)
{
    return src != HTTP_METHOD_NULL && (src & tar);
}

void HttpServletDispatch::handle(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto svl = RouterMatch(ctx);

    if(svl)
    {
        // 校验请求合法性
        if(svl->expectedMethod() == req->method()())
        {
            HTTP_F_DEBUG("conn[%s], path[%s] HttpServlet[%s] handling...... \n", conn->name().c_str(), req->path().c_str(), svl->name().c_str());
            svl->handle(conn, ctx);
        }
        else
        {
            BadRequest400Servlet svl400;

            HTTP_F_ERROR("http method invalid! %s 0x%x | %s ---> 0x%x \n", req->path().c_str(), MethodConver(req->method()()), req->method().toString(), svl->expectedMethod());
            
            svl400.handle(conn, ctx);
        }
    }
    else
    {
        HTTP_DEBUG() << req->path() << " default svl handle!" << std::endl;
        _defaultSvl->handle(conn, ctx);
    }


}

void HttpServletDispatch::addServlet(const std::string &url, HttpServlet::Ptr svl)
{
    HTTP_INFO() << url << "|" << svl->expectedMethod() << "|"<< "--->" << svl << std::endl;
    std::unique_lock<std::mutex> lock(_mapMtx);
    _servlets.emplace(url, svl);
}

void HttpServletDispatch::addServlet(const std::string &url, const FunctionServlet::CallBack &cb)
{
    std::unique_lock<std::mutex> lock(_mapMtx);
    _servlets.emplace(url, std::make_shared<FunctionServlet>(cb));
}

void HttpServletDispatch::delServlet(const std::string &url, int32_t method)
{
    std::unique_lock<std::mutex> lock(_mapMtx);
    auto svls = _servlets.equal_range(url);
    for(auto &it = svls.first;it != svls.second;++it)
    {
        if(it->second->expectedMethod() == method)
        {
            _servlets.erase(it);
            return;
        }
    }
    HTTP_DEBUG() << "delServlet faild: " << url << ", " << method << std::endl;
}

void HttpServletDispatch::delAllServlet(const std::string &url)
{
    std::unique_lock<std::mutex> lock(_mapMtx);
    auto n = _servlets.erase(url);
    HTTP_DEBUG() << "delAllServlet faild: " << url << ", size=" << n << std::endl;
}



HttpServlet::Ptr HttpServletDispatch::getServlet(const std::string &url, int32_t method)
{
    std::unique_lock<std::mutex> lock(_mapMtx);
    auto svls = _servlets.equal_range(url);
    for(auto &it = svls.first;it != svls.second;++it)
    {
        if(it->second->expectedMethod() == method)
        {
            return it->second;
        }
    }
    HTTP_DEBUG() << "getServlet faild: " << url << ", " << method << std::endl;
    return nullptr;
}
std::vector<HttpServlet::Ptr> HttpServletDispatch::getAllServlet(const std::string &url)
{
    std::vector<HttpServlet::Ptr> mv;
    std::unique_lock<std::mutex> lock(_mapMtx);
    auto svls = _servlets.equal_range(url);
    for(auto &it = svls.first; it != svls.second;++it)
    {
        mv.emplace_back(it->second);
    }

    return mv;

}

void HttpServletDispatch::addDynamicServlet(RouterMatcher::Ptr mathcer, HttpServlet::Ptr svl)
{
    std::unique_lock<std::mutex> lock(_vecMtx);

    auto it = std::find_if(_dynamicServlets.begin(), _dynamicServlets.end(), [=](const auto &rs) {

        HTTP_DEBUG() << "(" << rs.first->pattern() << ", " << rs.second->expectedMethod() << ")" << "--- (" << mathcer->pattern() << ", " << svl->expectedMethod() <<")" << std::endl;
        
        // 路径校验 + 方法校验
        return rs.first->pattern() == mathcer->pattern() 
            && rs.second->expectedMethod() == svl->expectedMethod();
    });

    if(it != _dynamicServlets.end())
    {
        it->first = mathcer;
        it->second = svl;
    }
    else
    {
        _dynamicServlets.emplace_back(mathcer, svl);
    }
    return;
}

void HttpServletDispatch::addDynamicServlet(RouterMatcher::Ptr mathcer, const FunctionServlet::CallBack &cb)
{
    std::unique_lock<std::mutex> lock(_vecMtx);
    auto svl = std::make_shared<FunctionServlet>(cb);

    addDynamicServlet(mathcer, svl);
    return;
}

void HttpServletDispatch::delDynamicServlet(const std::string &pattern_url, int32_t method)
{
    std::unique_lock<std::mutex> lock(_vecMtx);

    auto it = std::find_if(_dynamicServlets.begin(), _dynamicServlets.end(), [=](const auto &rs){
        return rs.first->pattern() == pattern_url
            && rs.second->expectedMethod() ==  method;
    });
    if(it != _dynamicServlets.end())
    {
        _dynamicServlets.erase(it);
    }
    else
    {
        HTTP_F_INFO("%d %s pattern url is not exist!", method,pattern_url.c_str());
    }
    return;
}

std::pair<RouterMatcher::Ptr, HttpServlet::Ptr>& HttpServletDispatch::getDynamicServlet(const std::string &pattern_url, int32_t method)
{
    std::unique_lock<std::mutex> lock(_vecMtx);

    auto it = std::find_if(_dynamicServlets.begin(), _dynamicServlets.end(), [=](const auto &rs){
        return rs.first->pattern() == pattern_url
            && rs.second->expectedMethod() ==  method;
    });
    return *it;
}


HttpServlet::Ptr HttpServletDispatch::RouterMatch(HttpContextPtr ctx)
{
    auto req = ctx->request();
    const std::string &url = req->path();

    {
        std::unique_lock<std::mutex> map_lock(_mapMtx);
        // 1. 精准匹配
        auto it = _servlets.find(url);
        if(it != _servlets.end())
            return it->second;
    }

    // 2. 模糊匹配 + 动态匹配
    {
        std::unique_lock<std::mutex> vec_lock(_vecMtx);

        for(int i = 0;i < _dynamicServlets.size();++i)
        {
            auto &it = _dynamicServlets[i];
            if(!it.first || !it.second)
            {
                HTTP_F_WARN("idx[%d] RouterMatcher is null! \n", i);
                continue;
            }

            const std::string& pattern1 = it.first->pattern();
            int32_t expectedMethod = it.second->expectedMethod();

            HTTP_DEBUG() << expectedMethod << ", " << pattern1 << std::endl;
            if((req->method()() ==  expectedMethod) && it.first->Match(ctx))
            {
                return it.second;
            }
        }
    }

    return nullptr;
}

/***********ServletDispatch************ */


}
   //kit_muduo