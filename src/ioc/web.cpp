/**
 * @file web.cpp
 * @brief web接口初始化
 * @author ljk5
 * @version 1.0
 * @date 2025-07-19 02:48:59
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "ioc/web.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_server.h"
#include "service/svc_auth.h"
#include "web/web_auth.h"
#include "web/web_project.h"
#include "web/web_protocol.h"
#include "web/web_user.h"
#include <vector>


using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_app {
std::shared_ptr<HttpServer> InitWebServer(kit_muduo::EventLoop *loop,
    ProjectHandler *projHdl,
    ProtocolHandler *protocHdl,
    AuthHandler *authHdl,
    UserHandler *userHdl,
    std::shared_ptr<AuthService> authSvc)
{

    // TODO 使用配置文件
    auto server = std::make_shared<HttpServer>(loop, InetAddress(5555), "http_server", true, TcpServer::Option::KReusePort);
    server->setThreadNum(4);

    auto static_file_svl = std::make_shared<StaticFileServlet>();
    //静态资源处理
    server->Get("/html/*.html", static_file_svl);
    server->Get("/css/*.css", static_file_svl);
    server->Get("/js/*.js", static_file_svl);
    server->Get("/assets/icons/*.svg", static_file_svl);

    server->setAuthCallback([authSvc](HttpContextPtr ctx) {
        const std::string path = ctx->request()->path();

        const std::vector<std::string> allow_paths_no_auth{
            "/html/login.html",
            "/css/login.css",
            "/js/namespace.js",
            "/js/config.js",
            "/js/utils.js",
            "/js/mock_data.js",
            "/js/api.js",
            "/js/auth.js",
            "/js/login.js",
            "/auth/login",
        };

        bool is_login_asset = false;
        
        for(auto &allow_path : allow_paths_no_auth)
        {
            if(allow_path == path)
            {
                is_login_asset = true;
                break;
            }
        }

        if(is_login_asset
            || path.find("/assets/") == 0)
        {
            return HttpServer::AuthCheckResult{};
        }

        const std::string cookie = ExtractCookieValue(ctx->request()->getHeader("Cookie"), "kit_session");
        auto current_user = authSvc->Authenticate(ctx, cookie);
        if(!current_user.has_value())
        {
            return HttpServer::AuthCheckResult{
                false,
                StateCode::k401Unauthorized,
                R"({"code":-401,"message":"unauthorized","data":{}})",
                path.find("/html/") == 0
            };
        }

        const bool is_admin_path = path.find("/users/") == 0
            || path == "/users/list"
            || path == "/users/add";
        if(is_admin_path && !current_user->IsAdmin())
        {
            return HttpServer::AuthCheckResult{
                false,
                StateCode::k403Forbidden,
                R"({"code":-403,"message":"forbidden","data":{}})",
                false
            };
        }

        return HttpServer::AuthCheckResult{};
    });

    authHdl->RegisterRoutes(server);
    userHdl->RegisterRoutes(server);
    projHdl->RegisterRoutes(server);
    protocHdl->RegisterRoutes(server);
    
    return server;
}


} // kit_domain
