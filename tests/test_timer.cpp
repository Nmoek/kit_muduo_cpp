/**
 * @file test_timer.cpp
 * @brief 定时器测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-27 21:40:22
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/timer.h"
#include "./test_log.h"
#include "base/time_stamp.h"
#include "net/sample_timer_queue.h"
#include "net/event_loop.h"

#include <gtest/gtest.h>

#include <vector>

using namespace kit_muduo;


TEST(TestTimer, DISABLED_test)
{
    Timer timer([](){
        TEST_DEBUG() << "cur time= " << TimeStamp::Now().toString() << ", timer is expired! \n" << std::endl;
    }, TimeStamp::NowMs(), 5);
    TEST_DEBUG() << "cur time= " << TimeStamp::Now().toString() << std::endl;
    sleep(6);
    if(TimeStamp::NowMs() >= timer.expiration())
        timer.run();
}
TEST(TestTimerQueue, test)
{
    // auto l = KIT_LOGGER("net");
    // l->setLevel(LogLevel::INFO);
    EventLoop loop;
    SampleTimerQueue queue(&loop);
    TEST_INFO() << "cur time=" << TimeStamp::Now().toString() << std::endl;
    for(int i = 1;i <= 3;++i)
    {
        queue.addTimer([=](){
            TEST_DEBUG() << "im timer "<< i << std::endl;
        }, TimeStamp::Now().addTime(2000));
    }

    auto timer4 = queue.addTimer([](){
        TEST_DEBUG() << "im timer 4" << std::endl;
    }, TimeStamp::Now().addTime(10000));

    queue.addTimer([&, timer4 = std::move(timer4)](){
        TEST_DEBUG() << "im timer 5" << std::endl;
        queue.cancel(timer4);
    }, TimeStamp::Now().addTime(5000));

    std::shared_ptr<Timer> timer6;
    int count = 3;
    // 间隔1s循环定时器
    timer6 = queue.addTimer([&](){
        TEST_DEBUG() << "im crc timer 6" << std::endl;
        if(count-- < 0)
        {
            queue.cancel(timer6); //定时器任务里不能够取消自己
            loop.quit();
        }
    }, TimeStamp::Now(), 3);

    TEST_DEBUG() << "loop start.." << std::endl;
    loop.loop();
}

TEST(TestTimerQueue, cancel_later_timer_in_same_expired_batch_skips_callback)
{
    EventLoop loop;
    SampleTimerQueue queue(&loop);
    std::vector<int> fired;
    std::shared_ptr<Timer> timer_b;

    TimeStamp when = TimeStamp::Now().addTime(20);

    queue.addTimer([&](){
        fired.push_back(1);
        queue.cancel(timer_b);
    }, when);

    timer_b = queue.addTimer([&](){
        fired.push_back(2);
    }, when);

    queue.addTimer([&](){
        loop.quit();
    }, TimeStamp::Now().addTime(120));

    loop.loop();

    ASSERT_EQ(fired.size(), 1);
    EXPECT_EQ(fired[0], 1);
}

TEST(TestTimerQueue, cancel_repeated_timer_in_same_expired_batch_prevents_reinsert)
{
    EventLoop loop;
    SampleTimerQueue queue(&loop);
    std::vector<int> fired;
    std::shared_ptr<Timer> repeated_timer;

    TimeStamp when = TimeStamp::Now().addTime(20);

    queue.addTimer([&](){
        fired.push_back(1);
        queue.cancel(repeated_timer);
    }, when);

    repeated_timer = queue.addTimer([&](){
        fired.push_back(2);
    }, when, 20);

    queue.addTimer([&](){
        loop.quit();
    }, TimeStamp::Now().addTime(140));

    loop.loop();

    ASSERT_EQ(fired.size(), 1);
    EXPECT_EQ(fired[0], 1);
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
