/**
 * @file example_http_server.cpp
 * @brief HTTP服务器使用示例
 * @author Kewin Li
 * @version 1.0
 * @date 2026-04-29 00:34:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/http/http_server.h"
#include "net/event_loop.h"

#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;


static void InitLog()
{
    KIT_LOGGER("base")->setLevel(LogLevel::WARN);
    KIT_LOGGER("net")->setLevel(LogLevel::INFO);
    KIT_LOGGER("web")->setLevel(LogLevel::WARN);
}


std::shared_ptr<HttpServer> HttpServerExample(kit_muduo::EventLoop *loop)
{
    const std::string& local_ip = InetAddress::GetInterfaceIpv4("eth0").toIp();

    auto server = std::make_shared<HttpServer>(loop, InetAddress(5555, local_ip), "http_server", true, TcpServer::Option::KReusePort);
    
    server->setThreadNum(4);
    
    return server;
}


int main()
{
    std::shared_ptr<HttpServer> server = nullptr;
    static EventLoop loop;

    try {
        InitLog();
        server = HttpServerExample(&loop);

    } catch(std::exception &e) {
        std::cerr << "application init fail!" << e.what() << std::endl;
        abort();
    }

    server->start();
    loop.loop();

    return 0;
}