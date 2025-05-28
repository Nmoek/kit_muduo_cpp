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

using namespace kit_muduo;


TEST(TestTimer, DISABLED_test)
{
    Timer timer([](){
        TEST_DEBUG() << "cur time= " << TimeStamp::Now().toString() << ", timer is expired! \n" << std::endl;
    }, TimeStamp::Now(), 5);
    TEST_DEBUG() << "cur time= " << TimeStamp::Now().toString() << std::endl;
    sleep(6);
    if(TimeStamp::Now() >= timer.expiration())
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
        }, TimeStamp::AddTime(TimeStamp::Now(), 2));
    }

    auto timer4 = queue.addTimer([](){
        TEST_DEBUG() << "im timer 4" << std::endl;
    }, TimeStamp::AddTime(TimeStamp::Now(), 10));

    queue.addTimer([&, timer4 = std::move(timer4)](){
        TEST_DEBUG() << "im timer 5" << std::endl;
        queue.cancel(timer4);
    }, TimeStamp::AddTime(TimeStamp::Now(), 5));

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


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}