/**
 * @file example_http_server.cpp
 * @brief HTTP服务器使用示例
 * @author Kewin Li
 * @version 1.0
 * @date 2026-04-29 00:34:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/call_backs.h"
#include "net/http/http_server.h"
#include "net/event_loop.h"
#include "net/net_log.h"
#include "net/http/http_servlet.h"
#include "net/websocket/websocket_session.h"

#include <memory>
#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace {

#define EXAMPLE_SERVER_PORT 8080
}

static void InitLog()
{
    // KIT_LOGGER("base")->setLevel(LogLevel::WARN);
    // KIT_LOGGER("net")->setLevel(LogLevel::INFO);
    // KIT_LOGGER("web")->setLevel(LogLevel::WARN);
}


std::shared_ptr<HttpServer> HttpServerExample(kit_muduo::EventLoop *loop)
{
    const std::string& local_ip = InetAddress::GetInterfaceIpv4("eth0").toIp();

    auto server = std::make_shared<HttpServer>(loop, InetAddress(EXAMPLE_SERVER_PORT, local_ip), "http_server", true, TcpServer::Option::KReusePort);
    
    server->setThreadNum(4);

    // 访问接口示例
    server->Get("/hello", std::make_shared<HelloServlet>());
    

    server->Ws("/ws/test", [](WebSocketSessionPtr session, HttpContextPtr ctx) -> bool {

        session->setWSOnOpenCb([](WebSocketSessionPtr session){
            session->sendText("hello im websocket!");
        });

        session->setWSTextMessageCb([](WebSocketSessionPtr session,const std::string& text){
            NET_F_INFO("example", "websocket session recv[%s]===> \n%s\n", session->peerAddr().toIpPort().c_str(), text.c_str());

            session->sendText(text);
        });

        session->setCloseCb([](WebSocketSessionPtr session){
            NET_F_INFO("example", "websocket session close... %ld: %s \n", session->sessionId(), session->peerAddr().toIpPort().c_str());
        });

        return true;
    });

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