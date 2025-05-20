/**
 * @file test_inet_address.cpp
 * @brief 网络地址类测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 22:46:27
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/inet_address.h"
#include "./test_log.h"

#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestTimeStamp, test)
{
    InetAddress addr(8489, "192.168.77.136");
    TEST_INFO() << "port= " << addr.toPort() << ", "
        << "ip= " << addr.toIp() << ", "
        << "ip:port= " << addr.toIpPort()
        << std::endl;
    // 发生隐式转换
    // InetAddress addr2 = 8191;
    // TEST_INFO() << "port= " << addr2.toPort() << std::endl;
    // output: port= 8191

}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}