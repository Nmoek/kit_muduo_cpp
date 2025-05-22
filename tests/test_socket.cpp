/**
 * @file test_socket.cpp
 * @brief socket测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 00:37:44
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/socket.h"
#include "net/inet_address.h"
#include "net/acceptor.h"
#include "./test_log.h"


#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestSocket, DISABLED_test1)
{
    Socket s(Socket::CreateIpv4());
    InetAddress addr(5555);
    s.bindAddress(addr);
    TEST_INFO() << "server listen.." << std::endl;
    s.listen();
    auto connfd = s.accept(&addr);
    TEST_INFO() << "new connect: " << connfd << ", " << addr.toIpPort() << std::endl;
}

TEST(TestSocket, test2)
{
    EventLoop loop;
    InetAddress addr(5555);
    Acceptor a(&loop, addr);

    a.setNewConnectionCallback([](int32_t connfd, const InetAddress &peerAddr){
        TEST_INFO() << "new connect: " << connfd << ", " << peerAddr.toIpPort() << std::endl;
    });
    a.listen();
    TEST_INFO() << "server listen.." << std::endl;
    loop.loop();
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}