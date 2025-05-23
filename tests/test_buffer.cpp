/**
 * @file test_buffer.cpp
 * @brief 读写缓存测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 02:55:52
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/buffer.h"
#include "./test_log.h"

#include <gtest/gtest.h>
#include <string.h>

using namespace kit_muduo;


TEST(TestBuffer, test)
{
    Buffer b;
    char data[] = {"sdfwerfwefewfew5f156few165f1ewf"};
    b.append(data, sizeof(data));

    std::string s = b.resetAllAsString();

    EXPECT_EQ(::memcmp(data, s.data(), sizeof(data)), 0);
    TEST_INFO() << "read data: " << s << std::endl;
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}