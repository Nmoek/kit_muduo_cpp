/**
 * @file test_event_loop_thread.cpp
 * @brief EventLoopThread线程类测试 + 池化测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 21:28:44
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/event_loop_thread.h"
#include "./test_log.h"


#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestEventLoopThread, test)
{
    EventLoop *loop = nullptr;
    EventLoopThread t([](EventLoop *loop){
        TEST_DEBUG() << "hello, loop= " << loop << std::endl;
    }, "EventLoopThread");

    loop = t.startLoop();
    TEST_DEBUG() << "get loop= " << loop << std::endl;
}

TEST(TestEventLoopThread, pool)
{
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}