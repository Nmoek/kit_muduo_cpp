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

/*
测试思路：该用例保留为人工调试样例，验证 Timer::run 的基本调用路径；因为
它会真实 sleep 6 秒，所以默认禁用，不作为稳定回归测试执行。

示例：到期后调用 timer.run() -> 回调打印一次当前日志时间。
*/
TEST(TestTimer, DISABLED_test)
{
    Timer timer([](){
        TEST_DEBUG() << "cur time= " << TimeStamp::Now().toLogString() << ", timer is expired! \n" << std::endl;
    }, TimeStamp::NowMs(), 5);
    TEST_DEBUG() << "cur time= " << TimeStamp::Now().toLogString() << std::endl;
    sleep(6);
    if(TimeStamp::NowMs() >= timer.expiration())
        timer.run();
}

/*
测试思路：启动 EventLoop，覆盖一次性、取消和重复定时器的组合行为；重复
定时器最终主动退出 loop，验证测试不会依赖外部线程停止事件循环。

示例：timer 5 在 5 秒取消 timer 4，timer 6 每 3ms 重复并在回调内退出。
*/
TEST(TestTimerQueue, test)
{
    // auto l = KIT_LOGGER("net");
    // l->setLevel(LogLevel::INFO);
    EventLoop loop;
    SampleTimerQueue queue(&loop);
    TEST_INFO() << "cur time=" << TimeStamp::Now().toLogString() << std::endl;
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

/*
测试思路：注册一个未来的墙上时间点，检查返回 Timer 保存的是当前单调时钟
上的 deadline，而不是 Unix epoch 毫秒。转换关系为：

  wall_target - wall_now -> delay
  monotonic_now + delay  -> timer.expiration()

示例：目标为当前墙上时间 +100ms，deadline 应落在单调时钟采样附近的未来
100ms 范围内，并且注册调用不会同步执行回调。
*/
TEST(TestTimerQueue, WallClockTargetIsConvertedToMonotonicDeadline)
{
    EventLoop loop;
    SampleTimerQueue queue(&loop);
    bool fired = false;

    const int64_t monotonic_before = TimeStamp::MonotonicNowMs();
    const TimeStamp target(TimeStamp::NowMs() + 100);
    auto timer = queue.addTimer([&fired]() {
        fired = true;
    }, target);
    const int64_t monotonic_after = TimeStamp::MonotonicNowMs();

    ASSERT_NE(timer, nullptr);
    EXPECT_FALSE(fired);
    EXPECT_GE(timer->expiration(), monotonic_before);
    EXPECT_LE(timer->expiration(), monotonic_after + 100);
}

/*
测试思路：两个同一批次到期的 timer 中，先执行的回调取消后执行的 timer，
后者不应再触发。

示例：批次 [timer 1, timer 2] -> timer 1 cancel(timer 2) -> fired == [1]。
*/
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

/*
测试思路：重复 timer 在首次到期批次中被另一个回调取消后，不应在 reset 阶段
重新插回队列。

示例：timer 1 cancel(repeated timer 2) -> 只记录 [1]，不会再次记录 [2]。
*/
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
