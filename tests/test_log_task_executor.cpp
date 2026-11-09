#include "base/log_task_executor.h"
#include "base/log_mmap_batch.h"
#include "log_test_support.h"

#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <pthread.h>
#include <set>
#include <thread>

using namespace kit_muduo;
using namespace log_test;
using namespace std::chrono_literals;

// 测试思路：逐项打破 CACHE 参数约束；例：initial=2/max=1、容量=0 均拒绝。
TEST(LogTaskExecutorTest, RejectsInvalidOptions)
{
    for (int field = 0; field < 4; ++field)
    {
        SCOPED_TRACE(field);
        LogTaskExecutor::Options options;
        if (field == 0) options.initial_threads = 0;
        if (field == 1) options.max_threads = 0;
        if (field == 2) options.queue_capacity = 0;
        if (field == 3) options.idle_seconds = 0;
        EXPECT_THROW(LogTaskExecutor executor(options), std::invalid_argument);
    }
}

// 测试思路：接收门仅在 start 后开启，join 幂等且关闭实例不可重启；例：恰好执行一次。
TEST(LogTaskExecutorTest, LifecycleRejectsOutsideAcceptanceWindow)
{
    std::atomic<int> calls{0};
    LogTaskExecutor executor({});
    EXPECT_FALSE(executor.trySubmit([&calls] { ++calls; }));
    executor.start();
    executor.start();
    EXPECT_FALSE(executor.trySubmit({}));
    ASSERT_TRUE(executor.trySubmit([&calls] { ++calls; }));
    executor.stopAccepting();
    EXPECT_FALSE(executor.trySubmit([&calls] { ++calls; }));
    EXPECT_TRUE(executor.drain(3000));
    executor.join();
    executor.join();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_THROW(executor.start(), std::logic_error);
}

// 测试思路：单线程卡住任务 A、队列仅容纳 B；C 拒绝且 drain 不能忽略正在运行的 A。
// A(门闩) -> B(排队) -> C(拒绝)；释放 A 后输出必须为 [A,B]。
TEST(LogTaskExecutorTest, BoundedQueueAndDrainIncludeRunningTask)
{
    Gate gate;
    std::vector<int> order;
    LogTaskExecutor executor({1, 1, 1, 30, "log-test"});
    ReleaseGateOnExit release(gate);
    executor.start();
    ASSERT_TRUE(executor.trySubmit([&gate, &order] { gate.enterAndWait(); order.push_back(1); }));
    ASSERT_TRUE(gate.waitForEntered());
    ASSERT_TRUE(executor.trySubmit([&order] { order.push_back(2); }));
    EXPECT_FALSE(executor.trySubmit([&order] { order.push_back(3); }));
    EXPECT_FALSE(executor.drain(10));
    executor.stopAccepting();
    gate.release();
    executor.join();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

// 测试思路：A 占住初始线程，再提交 B 触发扩容；例：上限 2 时两个门闩任务同时进入。
// 追加的 C 不能越过上限提前执行；worker 名称必须来自日志执行器而不是通用线程池。
TEST(LogTaskExecutorTest, GrowsToLimitAndNamesWorkers)
{
    Gate gate;
    std::mutex mutex;
    std::set<std::thread::id> workers;
    std::vector<std::string> names;
    std::atomic<int> third{0};
    LogTaskExecutor executor({1, 2, 4, 1, "log-test"});
    ReleaseGateOnExit release(gate);
    executor.start();
    const auto task = [&gate, &mutex, &workers, &names] {
        char name[16]{};
        ::pthread_getname_np(::pthread_self(), name, sizeof(name));
        {
            std::lock_guard<std::mutex> lock(mutex);
            workers.insert(std::this_thread::get_id());
            names.emplace_back(name);
        }
        gate.enterAndWait();
    };
    ASSERT_TRUE(executor.trySubmit(task));
    ASSERT_TRUE(gate.waitForEntered());
    ASSERT_TRUE(executor.trySubmit(task));
    ASSERT_TRUE(gate.waitForEntered(2));
    ASSERT_TRUE(executor.trySubmit([&third] { ++third; }));
    EXPECT_EQ(third.load(), 0);
    gate.release();
    executor.join();
    EXPECT_EQ(workers.size(), 2U);
    EXPECT_EQ(third.load(), 1);
    for (const auto& name : names) EXPECT_EQ(name.find("log-test-"), 0U);
}

// 测试思路：任务抛异常后 worker 仍执行队尾任务；例：throw -> 记录 42 -> drain 成功。
TEST(LogTaskExecutorTest, ThrowingTaskDoesNotLoseAcceptedSuccessor)
{
    std::atomic<int> result{0};
    LogTaskExecutor executor({1, 1, 4, 30, "log-test"});
    executor.start();
    ASSERT_TRUE(executor.trySubmit([] { throw std::runtime_error("injected task failure"); }));
    ASSERT_TRUE(executor.trySubmit([&result] { result = 42; }));
    EXPECT_TRUE(executor.drain(3000));
    executor.join();
    EXPECT_EQ(result.load(), 42);
}

// 测试思路：多个生产者与 join 竞争，比较接收数和完成数；例：拒绝的不计入、接收的恰好一次。
TEST(LogTaskExecutorTest, ConcurrentJoinPreservesEveryAcceptedTask)
{
    std::atomic<int> accepted{0};
    std::atomic<int> completed{0};
    Gate gate;
    LogTaskExecutor executor({1, 3, 32, 1, "log-test"});
    executor.start();
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i)
    {
        producers.emplace_back([&gate, &executor, &accepted, &completed] {
            gate.enterAndWait();
            for (int n = 0; n < 100; ++n)
                if (executor.trySubmit([&completed] { ++completed; })) ++accepted;
        });
    }
    EXPECT_TRUE(gate.waitForEntered(4));
    gate.release();
    executor.join();
    for (auto& producer : producers) producer.join();
    EXPECT_EQ(completed.load(), accepted.load());
    EXPECT_FALSE(executor.trySubmit([] {}));
}

// 测试思路：任务提交另一任务，验证业务闭包不在队列锁内执行；例：A -> submit B -> B=1。
TEST(LogTaskExecutorTest, TaskCanSubmitAnotherTask)
{
    std::atomic<int> completed{0};
    LogTaskExecutor executor({1, 1, 4, 30, "log-test"});
    executor.start();
    ASSERT_TRUE(executor.trySubmit([&executor, &completed] {
        EXPECT_TRUE(executor.trySubmit([&completed] { ++completed; }));
    }));
    EXPECT_TRUE(executor.drain(3000));
    executor.join();
    EXPECT_EQ(completed.load(), 1);
}

// 测试思路：真实 CACHE 线程扩至 2 后空闲退休到 1，再次积压可重新扩容；不以性能耗时作断言。
// 例：/proc/self/task 下 log-retire-* 线程数按 1 -> 2 -> 1 -> 2 -> 1 -> 0 变化。
TEST(LogTaskExecutorTest, RetiresIdleWorkersAndReusesSlotsAcrossGrowthCycles)
{
    const auto worker_count = [] {
        size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task"))
        {
            std::ifstream input(entry.path() / "comm");
            std::string name;
            std::getline(input, name);
            if (name.find("log-retire-") == 0) ++count;
        }
        return count;
    };
    const auto wait_count = [&worker_count](size_t expected) {
        const auto deadline = std::chrono::steady_clock::now() + 4s;
        while (worker_count() != expected && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        return worker_count() == expected;
    };
    LogTaskExecutor executor({1, 2, 4, 1, "log-retire"});
    executor.start();
    ASSERT_TRUE(wait_count(1));
    for (int round = 0; round < 2; ++round)
    {
        auto gate = std::make_shared<Gate>();
        ReleaseGateOnExit release(*gate);
        ASSERT_TRUE(executor.trySubmit([gate] { gate->enterAndWait(); }));
        ASSERT_TRUE(gate->waitForEntered());
        ASSERT_TRUE(executor.trySubmit([gate] { gate->enterAndWait(); }));
        ASSERT_TRUE(gate->waitForEntered(2));
        EXPECT_EQ(worker_count(), 2U);
        gate->release();
        ASSERT_TRUE(executor.drain(3000));
        EXPECT_TRUE(wait_count(1));
    }
    executor.join();
    EXPECT_TRUE(wait_count(0));
}

// 测试思路：A1 active 阻塞时 compression 可推进到 A2；同分支顺序均为 [1,2]。
// active: A1(wait) -> A2；compression: A1 -> A2(done)；drain 在释放前必须失败。
TEST(LogOrderedDispatcherTest, OrdersEachBranchIndependently)
{
    Gate gate;
    std::vector<int> active, compressed;
    std::promise<void> compression_done;
    auto ready = compression_done.get_future();
    LogBatchOrderedSinkDispatcher dispatcher;
    ReleaseGateOnExit release(gate);
    dispatcher.registerSink("A");
    dispatcher.start();
    ASSERT_TRUE(dispatcher.submitPair("A", [&gate, &active] {
        gate.enterAndWait(); active.push_back(1);
    }, [&compressed] { compressed.push_back(1); }));
    ASSERT_TRUE(gate.waitForEntered());
    ASSERT_TRUE(dispatcher.submitPair("A", [&active] { active.push_back(2); },
        [&compressed, &compression_done] { compressed.push_back(2); compression_done.set_value(); }));
    EXPECT_EQ(ready.wait_for(3s), std::future_status::ready);
    EXPECT_FALSE(dispatcher.drain(10));
    gate.release();
    dispatcher.stopAccepting();
    EXPECT_TRUE(dispatcher.drain(3000));
    dispatcher.wait();
    EXPECT_EQ(active, (std::vector<int>{1, 2}));
    EXPECT_EQ(compressed, (std::vector<int>{1, 2}));
    EXPECT_EQ(dispatcher.unexpectedFailures(), 0U);
}

// 测试思路：阻塞 sink A 的 active 后投递 sink B，B 必须独立完成；例：B 先于 A 释放。
TEST(LogOrderedDispatcherTest, DifferentSinksRunConcurrently)
{
    Gate gate;
    std::promise<void> done;
    auto ready = done.get_future();
    LogBatchOrderedSinkDispatcher dispatcher;
    ReleaseGateOnExit release(gate);
    dispatcher.start(false);
    ASSERT_TRUE(dispatcher.submitActive("A", [&gate] { gate.enterAndWait(); }));
    ASSERT_TRUE(gate.waitForEntered());
    ASSERT_TRUE(dispatcher.submitActive("B", [&done] { done.set_value(); }));
    EXPECT_EQ(ready.wait_for(3s), std::future_status::ready);
    EXPECT_FALSE(dispatcher.drain(10));
    gate.release();
    dispatcher.wait();
}

// 测试思路：compression 池只有一个 worker/一个排队位，A 占 worker、B 占队列。
// C 的 active pump 可入池但 compression 拒绝：双闭包都不能执行；释放后 C 可原样重投。
TEST(LogOrderedDispatcherTest, PairRollsBackWhenCompressionPoolRejects)
{
    Gate gate;
    std::atomic<int> active{0}, compressed{0};
    MmapPoolOptions options;
    options.compression_max_threads = 1;
    options.compression_queue_capacity = 1;
    LogBatchOrderedSinkDispatcher dispatcher(options);
    ReleaseGateOnExit release(gate);
    dispatcher.start();
    ASSERT_TRUE(dispatcher.submitCompression("A", [&gate] { gate.enterAndWait(); }));
    ASSERT_TRUE(gate.waitForEntered());
    ASSERT_TRUE(dispatcher.submitCompression("B", [] {}));
    EXPECT_FALSE(dispatcher.submitPair("C", [&active] { ++active; }, [&compressed] { ++compressed; }));
    gate.release();
    ASSERT_TRUE(dispatcher.drain(3000));
    EXPECT_EQ(active.load(), 0);
    EXPECT_EQ(compressed.load(), 0);
    ASSERT_TRUE(dispatcher.submitPair("C", [&active] { ++active; }, [&compressed] { ++compressed; }));
    dispatcher.wait();
    EXPECT_EQ(active.load(), 1);
    EXPECT_EQ(compressed.load(), 1);
}

// 测试思路：同 lane 的容量 3 包含正在执行的队头；例：A1 阻塞，A2/A3 接收，A4 拒绝。
TEST(LogOrderedDispatcherTest, LaneCapacityIncludesRunningHead)
{
    Gate gate;
    std::vector<int> order;
    LogBatchOrderedSinkDispatcher dispatcher;
    ReleaseGateOnExit release(gate);
    dispatcher.start(false);
    ASSERT_TRUE(dispatcher.submitActive("A", [&gate, &order] { gate.enterAndWait(); order.push_back(1); }));
    ASSERT_TRUE(gate.waitForEntered());
    ASSERT_TRUE(dispatcher.submitActive("A", [&order] { order.push_back(2); }));
    ASSERT_TRUE(dispatcher.submitActive("A", [&order] { order.push_back(3); }));
    EXPECT_FALSE(dispatcher.submitActive("A", [&order] { order.push_back(4); }));
    gate.release();
    dispatcher.wait();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

// 测试思路：关闭 compression 时整对提交必须拒绝，不能只执行 active；stop 后两类均拒绝。
// 例：pair(A,C)=false、active(A)=true；停止后新任务=0 次。
TEST(LogOrderedDispatcherTest, DisabledCompressionAndClosedGateRejectCleanly)
{
    std::atomic<int> calls{0};
    LogBatchOrderedSinkDispatcher dispatcher;
    EXPECT_THROW(dispatcher.registerSink(""), std::invalid_argument);
    dispatcher.registerSink("A");
    EXPECT_THROW(dispatcher.registerSink("A"), std::invalid_argument);
    EXPECT_FALSE(dispatcher.submitActive("A", [] {}));
    dispatcher.start(false);
    EXPECT_THROW(dispatcher.registerSink("B"), std::logic_error);
    EXPECT_FALSE(dispatcher.submitPair("A", [&calls] { ++calls; }, [] {}));
    EXPECT_FALSE(dispatcher.submitCompression("A", [] {}));
    EXPECT_FALSE(dispatcher.submitActive("", [] {}));
    EXPECT_FALSE(dispatcher.submitActive("A", {}));
    ASSERT_TRUE(dispatcher.submitActive("A", [&calls] { ++calls; }));
    dispatcher.stopAccepting();
    EXPECT_FALSE(dispatcher.submitActive("A", [&calls] { ++calls; }));
    dispatcher.wait();
    dispatcher.wait();
    EXPECT_THROW(dispatcher.start(), std::logic_error);
    EXPECT_EQ(calls.load(), 1);
}

// 测试思路：lane 任务抛异常仍须结清 outstanding 并推进后续任务；例：失败计数=1、B 完成。
TEST(LogOrderedDispatcherTest, TaskExceptionStillDrainsAndContinuesLane)
{
    std::atomic<int> calls{0};
    LogBatchOrderedSinkDispatcher dispatcher;
    dispatcher.start(false);
    ASSERT_TRUE(dispatcher.submitActive("A", [] { throw std::runtime_error("injected"); }));
    ASSERT_TRUE(dispatcher.submitActive("A", [&calls] { ++calls; }));
    EXPECT_TRUE(dispatcher.drain(3000));
    dispatcher.wait();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_EQ(dispatcher.unexpectedFailures(), 1U);
}
