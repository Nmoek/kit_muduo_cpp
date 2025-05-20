/**
 * @file test_muduo.cpp
 * @brief muduo 库使用示例
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 14:55:32
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include <gtest/gtest.h>
#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"

#include <string>

namespace mn = muduo::net;


class EchoServer
{
public:
    EchoServer(mn::EventLoop* loop,
        const mn::InetAddress& listenAddr,
        const std::string& nameArg,
        mn::TcpServer::Option option = mn::TcpServer::kNoReusePort)
        :_server(loop, listenAddr, nameArg, option)
        ,_loop(loop)
    {

    }

    void start()
    {
        _server.setConnectionCallback(std::bind(&EchoServer::onConnect, this, std::placeholders::_1));

        _server.setMessageCallback(std::bind(&EchoServer::onMessageCallback, this,
                    std::placeholders::_1,
                    std::placeholders::_2,
                    std::placeholders::_3));
        _server.setThreadNum(2);

        std::cout << "server ===>" << _server.ipPort() << " start.." << std::endl;

        _server.start();
        _loop->loop();

    }

private:

    void onConnect(const mn::TcpConnectionPtr& conn)
    {
        if(conn->connected())
        {
            std::cout << "new connection: " << conn->peerAddress().toIpPort() << std::endl;

        }
        else
        {
            conn->shutdown();
        }
    }

    void onMessageCallback(const mn::TcpConnectionPtr& conn,
                                mn::Buffer* buffer,
                                muduo::Timestamp receiveTime)
    {
        std::string msg = buffer->retrieveAllAsString();

        std::cout << "recv " << conn->peerAddress().toIpPort() << " msg: " << msg << std::endl;
        conn->send(msg);

    }

private:
    mn::TcpServer _server;
    mn::EventLoop *_loop;

};


TEST(TestMuduo, test)
{

    mn::EventLoop loop; // 等价于 epoll
    mn::InetAddress listenAddr("127.0.0.1", 8888);
    EchoServer server(&loop, listenAddr, "test_muduo");  // 等价于TCP  socket

    server.start();
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}