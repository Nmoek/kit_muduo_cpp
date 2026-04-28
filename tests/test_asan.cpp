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

#include "gtest/gtest.h"
#include <string.h>

using namespace kit_muduo;


TEST(TestASan, test)
{
    void *a = malloc(100);

    TEST_INFO() << "a=" << (void*)a << ",TestASan Over! " << std::endl;

    while(1) sleep(1);
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}