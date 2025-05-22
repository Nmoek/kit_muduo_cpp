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
#include "./test_log.h"


#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestSocket, test)
{
    Socket s(Socket::Create());
    InetAddress addr(5555);
    s.bindAddress(addr);
    TEST_INFO() << "server listen.." << std::endl;
    s.listen();
    auto connfd = s.accept(&addr);
    TEST_INFO() << "new connect: " << connfd << ", " << addr.toIpPort() << std::endl;
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}