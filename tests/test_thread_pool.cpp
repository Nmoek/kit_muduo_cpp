/**
 * @file test_thread_pool.cpp
 * @brief 工作线程池测试
 * @author Kewin Li
 * @version 2.0
 * @date 2026-09-18
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/thread_pool.h"

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace kit_muduo;
using namespace std::chrono_literals;

namespace {

constexpr auto kWaitTimeout = 2s;

class CountDownLatch
{
public:
    explicit CountDownLatch(int32_t count)
        : count_(count)
    {
    }

    void CountDown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ > 0 && --count_ == 0)
        {
            condition_.notify_all();
        }
    }

    bool WaitFor(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this]() {
            return count_ == 0;
        });
    }

private:
    int32_t count_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

}  // namespace

/*
测试思路：
1. 线程池没有启动时不允许接收任务，应返回 kStopping。
2. 拒绝结果不应携带一个伪装成可用结果的 future。
3. 该用例固定 submitTask 与 trySubmitTask 在未启动状态下的一致语义。

时序示意：
  constructed(not running) -> submit / trySubmit -> kStopping

示例：
  ThreadPool(1) 未调用 start()，提交返回 status=kStopping、future.valid()==false。
*/
TEST(TestThreadPool, RejectsTasksBeforeStart)
{
    ThreadPool pool(1);

    auto blocking_result = pool.submitTask([]() { return 1; });
    auto timed_result = pool.trySubmitTask(10, []() { return 2; });

    EXPECT_EQ(blocking_result.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(blocking_result.ok());
    EXPECT_FALSE(blocking_result.result_future.valid());
    EXPECT_EQ(timed_result.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(timed_result.ok());
    EXPECT_FALSE(timed_result.result_future.valid());
}

/*
测试思路：
1. 工作线程数量必须为正数，0 和负数都不能启动。
2. 异常应在 start() 边界同步抛出，便于调用方立即发现配置错误。
3. 启动失败后必须仍处于不可提交状态，stop() 和析构不应等待不存在的工作线程。

时序示意：
  ThreadPool(0/-1) -> start() -> invalid_argument -> submit:kStopping -> stop

示例：
  initThreadCount=0 或 -1 时，start() 均抛 std::invalid_argument，后续提交返回 kStopping。
*/
TEST(TestThreadPool, StartRejectsNonPositiveThreadCount)
{
    ThreadPool zero_thread_pool(0);
    ThreadPool negative_thread_pool(-1);

    EXPECT_THROW(zero_thread_pool.start(), std::invalid_argument);
    EXPECT_THROW(negative_thread_pool.start(), std::invalid_argument);

    auto zero_result = zero_thread_pool.submitTask([]() { return 0; });
    auto negative_result = negative_thread_pool.submitTask([]() { return -1; });

    EXPECT_EQ(zero_result.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(zero_result.result_future.valid());
    EXPECT_EQ(negative_result.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(negative_result.result_future.valid());
    EXPECT_NO_THROW(zero_thread_pool.stop());
    EXPECT_NO_THROW(negative_thread_pool.stop());
}

/*
测试思路：
1. 启动前设置的队列容量、最大线程数和空闲时间应能被读取。
2. 最大线程数不能小于初始线程数，设置更小值时应钳制到初始值。
3. 运行期间配置被冻结，setter 不应悄悄改变正在运行的线程池参数。

时序示意：
  configure -> start -> attempt reconfigure -> values unchanged

示例：
  初始线程数为 2，setThreadMaxThreshHold(1) 后读取值仍为 2。
*/
TEST(TestThreadPool, AppliesConfigurationBeforeStartAndFreezesItWhileRunning)
{
    ThreadPool pool(2);
    pool.setTaskQueMaxThreshHold(7);
    pool.setThreadMaxThreshHold(1);
    pool.setThreadMaxIdleInterval(3);

    EXPECT_EQ(pool.getTaskQueMaxThreshHold(), 7);
    EXPECT_EQ(pool.getThreadMaxThreshHold(), 2);
    EXPECT_EQ(pool.getThreadMaxIdleInterval(), 3);

    pool.start();
    pool.setTaskQueMaxThreshHold(9);
    pool.setThreadMaxThreshHold(10);
    pool.setThreadMaxIdleInterval(5);

    EXPECT_EQ(pool.getTaskQueMaxThreshHold(), 7);
    EXPECT_EQ(pool.getThreadMaxThreshHold(), 2);
    EXPECT_EQ(pool.getThreadMaxIdleInterval(), 3);
}

/*
测试思路：
1. 校验普通参数绑定、返回值传递以及 void 返回任务三条基础路径。
2. future.get() 必须取得真实任务结果，并能等待 void 任务完成。
3. 使用原子计数确认 void 任务确实执行，而不是只完成了提交动作。

时序示意：
  submit(add/string/void) -> workers execute -> future.get()

示例：
  (20, 22) -> 42；("kit", 2) -> "kit2"；void 任务令计数从 0 变为 1。
*/
TEST(TestThreadPool, ExecutesTasksAndReturnsTypedResults)
{
    ThreadPool pool(2);
    pool.start();
    std::atomic_int void_task_count{0};

    auto integer_result = pool.submitTask([](int lhs, int rhs) {
        return lhs + rhs;
    }, 20, 22);
    auto string_result = pool.submitTask([](const std::string &prefix, int suffix) {
        return prefix + std::to_string(suffix);
    }, std::string("kit"), 2);
    auto void_result = pool.submitTask([&void_task_count]() {
        ++void_task_count;
    });

    ASSERT_TRUE(integer_result.ok());
    ASSERT_TRUE(string_result.ok());
    ASSERT_TRUE(void_result.ok());
    EXPECT_EQ(integer_result.result_future.get(), 42);
    EXPECT_EQ(string_result.result_future.get(), "kit2");
    EXPECT_NO_THROW(void_result.result_future.get());
    EXPECT_EQ(void_task_count.load(), 1);
}

/*
测试思路：
1. 固定模式配置 3 个线程，并提交 3 个等待同一释放信号的任务。
2. 释放信号发出前，三个任务都必须已经进入执行态，证明不是串行消费。
3. 无论前置断言是否成功都发送释放信号，避免失败时析构永久等待。

时序示意：
  worker1 -- task1 --\\
  worker2 -- task2 ----> all_started -> release -> all futures ready
  worker3 -- task3 --/

示例：
  3 个任务都在 2 秒内到达门闩，然后分别返回 1、2、3。
*/
TEST(TestThreadPool, FixedModeRunsUpToConfiguredConcurrency)
{
    ThreadPool pool(3);
    pool.start();
    CountDownLatch all_started(3);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();
    std::vector<ThreadPool::SubmitResult<int>> results;

    for(int value = 1; value <= 3; ++value)
    {
        results.push_back(pool.submitTask([&all_started, release, value]() {
            all_started.CountDown();
            release.wait();
            return value;
        }));
        ASSERT_TRUE(results.back().ok());
    }

    const bool started_concurrently = all_started.WaitFor(kWaitTimeout);
    release_promise.set_value();

    EXPECT_TRUE(started_concurrently);
    for(int index = 0; index < 3; ++index)
    {
        EXPECT_EQ(results[index].result_future.get(), index + 1);
    }
}

/*
测试思路：
1. 用一个门控任务占住唯一工作线程，再放入一个任务占满容量为 1 的队列。
2. 第三个任务使用短超时提交，此时既无空闲线程也无队列槽位，应返回 kTimeout。
3. 超时任务不能被执行，也不能返回有效 future；释放门控后前两个任务正常完成。

时序示意：
  worker: [task1 blocked]
  queue : [task2 full] <- task3 trySubmit(50ms) -> kTimeout

示例：
  单线程、队列容量 1 时，第三次提交超时，executed_count 最终仍为 2。
*/
TEST(TestThreadPool, TimedSubmitReportsTimeoutWhenQueueRemainsFull)
{
    ThreadPool pool(1);
    pool.setTaskQueMaxThreshHold(1);
    pool.start();
    CountDownLatch first_started(1);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();
    std::atomic_int executed_count{0};

    auto first = pool.submitTask([&first_started, release, &executed_count]() {
        first_started.CountDown();
        release.wait();
        return ++executed_count;
    });
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(first_started.WaitFor(kWaitTimeout));

    auto second = pool.submitTask([&executed_count]() {
        return ++executed_count;
    });
    ASSERT_TRUE(second.ok());

    auto timed_out = pool.trySubmitTask(50, [&executed_count]() {
        return ++executed_count;
    });
    release_promise.set_value();

    EXPECT_EQ(timed_out.status, ThreadPool::SubmitStatus::kTimeout);
    EXPECT_FALSE(timed_out.ok());
    EXPECT_FALSE(timed_out.result_future.valid());
    EXPECT_EQ(first.result_future.get(), 1);
    EXPECT_EQ(second.result_future.get(), 2);
    EXPECT_EQ(executed_count.load(), 2);
}

/*
测试思路：
1. 先构造“一个任务执行中、一个任务排队中”的满队列，再从其他线程阻塞提交第三个任务。
2. stop() 必须唤醒等待 notFull_ 的提交者，使其返回 kStopping，而不是永久阻塞。
3. stop() 同时应等待已经接收的两个任务执行完；最后释放首任务并检查停止完成。

时序示意：
  submitter -- wait(notFull) --\\
                               stop -> submitter:kStopping
  worker ---- task1 blocked --- release -> task2 -> exit -> stop returns

示例：
  第三个 submitTask 在停止后被拒绝，前两个 future 仍分别返回 1 和 2。
*/
TEST(TestThreadPool, StopWakesBlockedSubmitterAndDrainsAcceptedTasks)
{
    ThreadPool pool(1);
    pool.setTaskQueMaxThreshHold(1);
    pool.start();
    CountDownLatch first_started(1);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto first = pool.submitTask([&first_started, release]() {
        first_started.CountDown();
        release.wait();
        return 1;
    });
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(first_started.WaitFor(kWaitTimeout));
    auto second = pool.submitTask([]() { return 2; });
    ASSERT_TRUE(second.ok());

    auto blocked_submit = std::async(std::launch::async, [&pool]() {
        return pool.submitTask([]() { return 3; });
    });
    auto stop_result = std::async(std::launch::async, [&pool]() {
        pool.stop();
    });

    const auto submit_status = blocked_submit.wait_for(kWaitTimeout);
    if(submit_status != std::future_status::ready)
    {
        release_promise.set_value();
        stop_result.wait();
        FAIL() << "blocked submitter was not awakened by stop()";
        return;
    }
    auto rejected = blocked_submit.get();
    EXPECT_EQ(rejected.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(rejected.result_future.valid());

    release_promise.set_value();
    const auto stop_status = stop_result.wait_for(kWaitTimeout);
    ASSERT_EQ(stop_status, std::future_status::ready);
    EXPECT_NO_THROW(stop_result.get());
    EXPECT_EQ(first.result_future.get(), 1);
    EXPECT_EQ(second.result_future.get(), 2);
}

/*
测试思路：
1. packaged_task 应把业务异常保存到 future，而不是让工作线程退出。
2. 在抛异常任务之后再提交普通任务，确认同一个线程池仍能继续消费。
3. future.get() 应按原类型重新抛出异常，调用方不能收到默认值或 broken_promise。

时序示意：
  throwing task -> future stores runtime_error -> worker survives -> next task returns 7

示例：
  throw runtime_error("task failed") 后，首 future 抛异常，第二个 future 返回 7。
*/
TEST(TestThreadPool, TaskExceptionPropagatesThroughFutureWithoutBreakingWorker)
{
    ThreadPool pool(1);
    pool.start();

    auto failed = pool.submitTask([]() -> int {
        throw std::runtime_error("task failed");
    });
    auto following = pool.submitTask([]() { return 7; });

    ASSERT_TRUE(failed.ok());
    ASSERT_TRUE(following.ok());
    EXPECT_THROW(failed.result_future.get(), std::runtime_error);
    EXPECT_EQ(following.result_future.get(), 7);
}

/*
测试思路：
1. 缓存模式从 1 个初始线程开始，每当现有线程被门控任务占用时再提交一个任务。
2. 三个任务必须在释放前同时进入执行态，证明线程池按负载扩容到最大值 3。
3. 释放后检查所有 future，确保扩容过程中没有遗漏任务或破坏结果。

时序示意：
  initial worker busy -> submit task2 -> add worker2 busy
                      -> submit task3 -> add worker3 busy -> release all

示例：
  init=1、max=3，三个阻塞任务均在 2 秒内启动并返回 1、2、3。
*/
TEST(TestThreadPool, CacheModeExpandsWorkersUnderBacklog)
{
    ThreadPool pool(1);
    pool.setMode(ThreadPool::CACHE_MOD);
    pool.setThreadMaxThreshHold(3);
    pool.start();
    CountDownLatch all_started(3);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();
    std::vector<ThreadPool::SubmitResult<int>> results;

    for(int value = 1; value <= 3; ++value)
    {
        results.push_back(pool.submitTask([&all_started, release, value]() {
            all_started.CountDown();
            release.wait();
            return value;
        }));
        ASSERT_TRUE(results.back().ok());
    }

    const bool expanded_to_three_workers = all_started.WaitFor(kWaitTimeout);
    release_promise.set_value();

    EXPECT_TRUE(expanded_to_three_workers);
    for(int index = 0; index < 3; ++index)
    {
        EXPECT_EQ(results[index].result_future.get(), index + 1);
    }
}

/*
测试思路：
1. 缓存模式先扩容到 2 个线程，然后等待超过配置的 1 秒空闲回收周期。
2. 回收扩展线程后，线程池仍应保留初始线程并能继续接收、执行任务。
3. 该用例从公开行为验证“回收不破坏池”，不依赖私有线程计数实现细节。

时序示意：
  expand to 2 -> tasks finish -> idle > 1s -> extra worker exits -> submit again

示例：
  空闲 1300ms 后提交返回 9，而不是超时、broken_promise 或永久等待。
*/
TEST(TestThreadPool, CacheModeRemainsUsableAfterIdleWorkerReclamation)
{
    ThreadPool pool(1);
    pool.setMode(ThreadPool::CACHE_MOD);
    pool.setThreadMaxThreshHold(2);
    pool.setThreadMaxIdleInterval(1);
    pool.start();
    CountDownLatch both_started(2);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto first = pool.submitTask([&both_started, release]() {
        both_started.CountDown();
        release.wait();
    });
    auto second = pool.submitTask([&both_started, release]() {
        both_started.CountDown();
        release.wait();
    });
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    const bool expanded = both_started.WaitFor(kWaitTimeout);
    release_promise.set_value();
    ASSERT_TRUE(expanded);
    ASSERT_NO_THROW(first.result_future.get());
    ASSERT_NO_THROW(second.result_future.get());

    std::this_thread::sleep_for(1300ms);
    auto after_reclamation = pool.submitTask([]() { return 9; });

    ASSERT_TRUE(after_reclamation.ok());
    EXPECT_EQ(after_reclamation.result_future.get(), 9);
}

/*
测试思路：
1. 线程池运行期间再次调用 start() 应直接返回，不能重复创建初始工作线程。
2. 用第一个门控任务占住唯一 worker，第二个任务在释放前必须保持未完成。
3. 如果重复 start() 错误地创建了第二个 worker，第二个任务会提前完成并使测试失败。

时序示意：
  start -> start(no-op) -> worker1:task1(blocked) -> task2 queued -> release

示例：
  initThreadCount=1，重复 start() 后 task2 在 task1 释放前仍为 timeout。
*/
TEST(TestThreadPool, RepeatedStartWhileRunningDoesNotCreateMoreWorkers)
{
    ThreadPool pool(1);
    pool.start();
    EXPECT_NO_THROW(pool.start());

    CountDownLatch first_started(1);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto first = pool.submitTask([&first_started, release]() {
        first_started.CountDown();
        release.wait();
        return 1;
    });
    ASSERT_TRUE(first.ok());

    const bool started = first_started.WaitFor(kWaitTimeout);
    if(!started)
    {
        release_promise.set_value();
        FAIL() << "the first task did not start";
        return;
    }

    auto second = pool.submitTask([]() { return 2; });
    const bool second_accepted = second.ok();
    auto second_status = std::future_status::deferred;
    if(second_accepted)
    {
        second_status = second.result_future.wait_for(100ms);
    }

    release_promise.set_value();

    ASSERT_TRUE(second_accepted);
    EXPECT_EQ(second_status, std::future_status::timeout);
    EXPECT_EQ(first.result_future.get(), 1);
    EXPECT_EQ(second.result_future.get(), 2);
}

/*
测试思路：
1. ThreadPool 是一次性生命周期，stop() 后不能再次进入 kRunning。
2. 停止后的任务提交必须持续返回 kStopping，不能因再次调用 start() 而恢复。
3. 重复 stop() 与停止后的 start() 都应安全返回，不得重复 join 或创建线程。

时序示意：
  kInit -> start -> kRunning -> stop -> kStop
                                      |-> stop(no-op)
                                      |-> start(no-op) -> submit:kStopping

示例：
  第一轮任务返回 1；停止后再次 start，后续任务仍被拒绝且 future 无效。
*/
TEST(TestThreadPool, StopIsIdempotentAndStoppedPoolCannotRestart)
{
    ThreadPool pool(1);
    pool.start();

    auto first = pool.submitTask([]() { return 1; });
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(first.result_future.get(), 1);

    pool.stop();
    auto stopped = pool.submitTask([]() { return 2; });
    EXPECT_EQ(stopped.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(stopped.result_future.valid());
    EXPECT_NO_THROW(pool.stop());

    EXPECT_NO_THROW(pool.start());
    auto after_restart_attempt = pool.submitTask([]() { return 3; });
    EXPECT_EQ(after_restart_attempt.status, ThreadPool::SubmitStatus::kStopping);
    EXPECT_FALSE(after_restart_attempt.result_future.valid());
}

/*
测试思路：
1. 同步 stop() 会等待所有 busy worker，因此工作线程不能停止自己。
2. worker 内误调 stop() 应快速抛出 logic_error，由 packaged_task 保存到 future。
3. 异常不能杀死 worker；后续任务仍应执行，最后由外部线程完成 stop()。

时序示意：
  worker: task -> stop() -> logic_error -> future
          next task -> 7
  owner : future.get() throws -> external stop() -> joined

示例：
  任务内调用 pool.stop() 时 future.get() 抛 logic_error，下一任务仍返回 7。
*/
TEST(TestThreadPool, WorkerCallingStopFailsFastWithoutBreakingPool)
{
    ThreadPool pool(1);
    pool.start();

    auto self_stop = pool.submitTask([&pool]() {
        pool.stop();
    });
    ASSERT_TRUE(self_stop.ok());
    EXPECT_THROW(self_stop.result_future.get(), std::logic_error);

    auto following = pool.submitTask([]() { return 7; });
    ASSERT_TRUE(following.ok());
    EXPECT_EQ(following.result_future.get(), 7);
    EXPECT_NO_THROW(pool.stop());
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
