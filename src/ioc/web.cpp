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
#include "app_config.h"

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;


namespace kit_app {

namespace {
WebServerStartupConfig MakeWebServerStartupConfig()
{
    WebServerStartupConfig config;
#define XX(VAR) \
    config.VAR = *(APP_CONFIG_VARS_SYSTEM_HTTP(VAR)->value())

    XX(host);
    XX(port);
    XX(io_threads);
#undef XX

    config.static_root = *(
        APP_CONFIG_VARS_SYSTEM_HTTP(static_root_path)->value());

#define XX(VAR) \
    config.business_thread_pool.VAR = *(APP_CONFIG_VARS_SYSTEM_BUSINESS(VAR)->value())

    XX(max_threads);
    XX(max_task_queue);
    XX(thread_idle_seconds);
    XX(submit_timeout_ms);
#undef XX
    return config;
}

} // namespace

std::shared_ptr<kit_muduo::http::HttpServer> InitWebServer(kit_muduo::EventLoop *loop,
    ProjectHandler *projHdl,
    ProtocolHandler *protocHdl,
    AuthHandler *authHdl,
    UserHandler *userHdl,
    ProtocolInteractionHandler *interHdl)
{
    const auto& web_server_config = MakeWebServerStartupConfig();

    auto server = std::make_shared<HttpServer>(loop,
        InetAddress(web_server_config.port, web_server_config.host),
        "http_server",
        true,
        TcpServer::Option::KReusePort);

    // IO线程组配置
    server->setThreadNum(web_server_config.io_threads);

    // 业务线程池配置
    server->setBusinessThreadPoolConfig(web_server_config.business_thread_pool);

    auto static_file_svl = std::make_shared<StaticFileServlet>(web_server_config.static_root);
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
