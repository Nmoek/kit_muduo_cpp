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
    const WebServerStartupConfig& startup_config,
    ProjectHandler *projHdl,
    ProtocolHandler *protocHdl,
    AuthHandler *authHdl,
    UserHandler *userHdl,
    ProtocolInteractionHandler *interHdl)
{

    auto server = std::make_shared<HttpServer>(loop,
        InetAddress(startup_config.port, startup_config.host),
        "http_server",
        true,
        TcpServer::Option::KReusePort);

    // IO线程组配置
    server->setThreadNum(startup_config.io_threads);

    // 业务线程池配置
    server->setBusinessThreadPoolConfig(startup_config.business_thread_pool);

    auto static_file_svl = std::make_shared<StaticFileServlet>(startup_config.static_root);
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
