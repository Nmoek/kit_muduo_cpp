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
#include "web/web_protocol_interaction.h"


using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_app {
std::shared_ptr<kit_muduo::http::HttpServer> InitWebServer(kit_muduo::EventLoop *loop,
    ProjectHandler *projHdl,
    ProtocolHandler *protocHdl,
    AuthHandler *authHdl,
    UserHandler *userHdl,
    ProtocolInteractionHandler *interHdl)
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



    authHdl->RegisterRoutes(server);
    userHdl->RegisterRoutes(server);
    projHdl->RegisterRoutes(server);
    protocHdl->RegisterRoutes(server);
    interHdl->RegisterRoutes(server);
    
    return server;
}


} // kit_domain
