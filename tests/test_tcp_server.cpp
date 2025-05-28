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
    // server.setThreadNum(4);

    std::unordered_map<int32_t, std::shared_ptr<Timer>> timer_map;

    server.setConnectionCallback([&](const TcpConnectionPtr& conn){
        if(conn->connected())
        {
            TEST_INFO() << "new connection!" << std::endl;
            auto timer = conn->getLoop()->runEvery(5, [conn](){
                std::stringstream ss;
                ss << "this is a eveny timer msg! ";
                ss << TimeStamp::NowTimeStamp();
                ss << std::endl;
                TEST_INFO() << ss.str();
                conn->send(ss.str());
            });
            // 不记录无法知道定时器是哪一个
            timer_map[conn->fd()] = timer;
        }
        else
        {
            TEST_INFO() << "=============connection close!!!" << std::endl;
            conn->getLoop()->cancel(timer_map[conn->fd()]);
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