/**
 * @file test_thread.cpp
 * @brief 线程类测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 13:13:58
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/thread.h"
#include "./test_log.h"


#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestThread, test)
{

    Thread t([](){
        TEST_INFO() << "hello im thread" << std::endl;
    }, "my_thread");
    TEST_INFO() << "thread test start" << std::endl;
    t.start();
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}