/**
 * @file test_tcp_server.cpp
 * @brief TcpServer测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 00:12:23
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/tcp_server.h"
#include "./test_log.h"
#include "net/event_loop.h"
#include "net/inet_address.h"

#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestTcpServer, test)
{
    // auto l = KIT_LOGGER("net");
    // l->setLevel(LogLevel::INFO);
    EventLoop loop;
    InetAddress addr(5555);
    TcpServer server(&loop, addr, "mytcp_server", TcpServer::KReusetPort);
    server.setThreadNum(4);

    server.setConnectionCallback([](const TcpConnectionPtr& conn){
        if(conn->connected())
        {
            TEST_INFO() << "new connection!" << std::endl;
        }
        else
        {
            conn->shutdown();
        }
    });

    server.setMessageCallback([](const TcpConnectionPtr &conn, Buffer* buffer, TimeStamp receiveTime){
        std::string msg = buffer->resetAllAsString();
        msg.pop_back();

        TEST_INFO() << "recv msg from " << conn->peerAddr().toIpPort() << ", " << msg << std::endl;

        msg.push_back('\n');
        conn->send(msg);
    });

    server.start();
    loop.loop();
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}