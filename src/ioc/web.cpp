/**
 * @file web.cpp
 * @brief web接口初始化
 * @author ljk5
 * @version 1.0
 * @date 2025-07-19 02:48:59
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "ioc/web.h"
#include "net/http/http_server.h"
#include "web/web_project.h"
#include "web/web_protocol.h"


using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_app {
std::shared_ptr<HttpServer> InitWebServer(kit_muduo::EventLoop *loop,
    ProjectHandler *projHdl,
    ProtocolHandler *protocHdl)
{

    const std::string& local_ip = InetAddress::GetInterfaceIpv4("eth0").toIp();

    // TODO 使用配置文件
    auto server = std::make_shared<HttpServer>(loop, InetAddress(5555, local_ip), "http_server", true, TcpServer::Option::KReusePort);
    server->setThreadNum(4);

    auto static_svl = std::make_shared<StaticFileServlet>();
    //静态资源处理
    server->Get("*.html", static_svl);
    server->Get("*.css", static_svl);
    server->Get("*.js", static_svl);


    projHdl->RegisterRoutes(server);
    protocHdl->RegisterRoutes(server);
    
    return server;
}


} // kit_domain