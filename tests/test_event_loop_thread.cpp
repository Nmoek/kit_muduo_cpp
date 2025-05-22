/**
 * @file test_event_loop_thread.cpp
 * @brief EventLoop事件循环测试 +EventLoopThread线程类测试 + 池化测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 21:28:44
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/event_loop_thread.h"
#include "base/event_loop_thread_pool.h"

#include "./test_log.h"


#include <gtest/gtest.h>

using namespace kit_muduo;



TEST(TestEventLoopThread, thread)
{
    EventLoop *loop = nullptr;
    EventLoopThread t([](EventLoop *loop){
        TEST_DEBUG() << "hello, loop= " << loop << std::endl;
    }, "EventLoopThread");

    // 理解难点：把线程内部实际运行的事件循环EventLoop "偷"到外面
    loop = t.startLoop();
    TEST_DEBUG() << "get loop= " << loop << std::endl;
}

TEST(TestEventLoopThread, pool)
{
    EventLoop base_loop;
    EventLoopThreadPool pool(&base_loop, "test_pool1");
    pool.start([](EventLoop *loop){
        TEST_INFO() << "hello this is pool" << std::endl;
    });

    EventLoopThreadPool pool2(&base_loop, "test_pool2");
    pool2.setThreadNum(3);
    pool2.start([](EventLoop *loop){
        TEST_INFO() << "hello this is pool2" << std::endl;
    });

}

TEST(TestEventLoopThread, loop)
{
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}