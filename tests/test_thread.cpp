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
#include "base/thread_group.h"

#include "gtest/gtest.h"
#include <chrono>
#include <atomic>
#include <future>
#include <memory>
#include <system_error>
#include <stdexcept>
#include <thread>

using namespace kit_muduo;


// 测试思路：验证基础 Thread 可以启动并执行传入函数。Thread 析构时
// 负责处理尚未显式 join 的线程，作为 ThreadGroup 底层行为的基线。
TEST(TestThread, test)
{

    Thread t([](){
        TEST_INFO() << "hello im thread" << std::endl;
    }, "my_thread");
    TEST_INFO() << "thread test start" << std::endl;
    t.start();
}

// 测试思路：两个任务先分别报告已进入，再共同等待主线程放行。只有
// 两个任务同时存活时，两份进入通知才会同时就绪；随后验证同一线程组
// 可以分别返回 string 和 int 两种强类型业务结果。
TEST(TestThreadGroup, MultiWrite)
{
    std::atomic_int32_t num{0};
    auto start_gate = std::make_shared<std::promise<void>>();
    auto start_signal = start_gate->get_future().share();
    auto add1_entered = std::make_shared<std::promise<void>>();
    auto add2_entered = std::make_shared<std::promise<void>>();
    auto add1_entered_future = add1_entered->get_future();
    auto add2_entered_future = add2_entered->get_future();

    auto add1 = ThreadGroup::MakePackage("add1", [&num, add1_entered, start_signal](const ThreadGroup::StopPredicate&) {
        add1_entered->set_value();
        start_signal.wait();
        num.fetch_add(1);
        return std::string("add1-result");
    });

    auto add2 = ThreadGroup::MakePackage("add2", [&num, add2_entered, start_signal](const ThreadGroup::StopPredicate&) {
        add2_entered->set_value();
        start_signal.wait();
        num.fetch_add(2);
        return int32_t{2};
    });

    ThreadGroup tg({
        std::move(add1.package),
        std::move(add2.package)
    });
    tg.startAll();

    const auto add1_status =
        add1_entered_future.wait_for(std::chrono::seconds(1));
    const auto add2_status =
        add2_entered_future.wait_for(std::chrono::seconds(1));
    start_gate->set_value();
    tg.wait();

    ASSERT_EQ(add1_status, std::future_status::ready);
    ASSERT_EQ(add2_status, std::future_status::ready);
    ASSERT_EQ(add1.future.get(), "add1-result");
    ASSERT_EQ(add2.future.get(), 2);
    ASSERT_EQ(num.load(), 3);
    ASSERT_TRUE(tg.joined());
}

// 测试思路：packaged_task 应支持 void 返回值，并且 future.get() 在
// wait() 完成后正常返回。用原子变量验证任务确实被线程执行。
TEST(TestThreadGroup, VoidTaskReturnsThroughFuture)
{
    std::atomic_bool executed{false};

    auto task = ThreadGroup::MakePackage(
        "void-task",
        [&executed](const ThreadGroup::StopPredicate& stop) {
            if (stop()) {
                return;
            }
            executed.store(true, std::memory_order_release);
        });

    ThreadGroup group({std::move(task.package)});
    group.startAll();
    group.wait();

    ASSERT_NO_THROW(task.future.get());
    ASSERT_TRUE(executed.load(std::memory_order_acquire));
    ASSERT_TRUE(group.joined());
}

// 测试思路：用户任务抛出的异常应由 packaged_task 保存，不能被
// ThreadGroup 吞掉；调用方在对应 future.get() 处重新得到异常。
TEST(TestThreadGroup, TaskExceptionIsDeliveredByFuture)
{
    auto task = ThreadGroup::MakePackage(
        "throwing-task",
        [](const ThreadGroup::StopPredicate&) -> std::string {
            throw std::runtime_error("task failed");
        });

    ThreadGroup group({std::move(task.package)});
    group.startAll();
    group.wait();

    try {
        (void)task.future.get();
        FAIL() << "future.get() should rethrow task exception";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "task failed");
    } catch (...) {
        FAIL() << "future.get() returned an unexpected exception type";
    }
}

// 测试思路：requestStop() 只发布协作式停止请求。任务持续检查
// StopPredicate，收到请求后返回一个业务结果，wait() 再负责回收线程。
TEST(TestThreadGroup, RequestStopIsObservedByTask)
{
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();

    auto task = ThreadGroup::MakePackage(
        "stoppable-task",
        [entered](const ThreadGroup::StopPredicate& stop) -> bool {
            entered->set_value();
            while (!stop()) {
                std::this_thread::yield();
            }
            return stop();
        });

    ThreadGroup group({std::move(task.package)});
    group.startAll();

    ASSERT_EQ(
        entered_future.wait_for(std::chrono::seconds(1)),
        std::future_status::ready);

    group.requestStop();
    group.wait();

    ASSERT_TRUE(task.future.get());
    ASSERT_TRUE(group.joined());
    ASSERT_TRUE(group.stopping());
}

// 测试思路：一次性线程组必须拒绝空任务集合；异常应在启动阶段
// 暴露，而不是静默返回。
TEST(TestThreadGroup, EmptyPackageListIsRejected)
{
    ThreadGroup group(std::vector<ThreadGroup::Package>{});

    EXPECT_THROW(group.startAll(), std::invalid_argument);
    EXPECT_FALSE(group.joined());
}

// 测试思路：ThreadGroup 只能启动一次。第一次启动并等待后，第二次
// 启动不应创建第二批线程或重复执行 packaged_task。
TEST(TestThreadGroup, StartAllCanOnlyBeCalledOnce)
{
    std::atomic_int executions{0};

    auto task = ThreadGroup::MakePackage(
        "once-task",
        [&executions](const ThreadGroup::StopPredicate&) -> int {
            return ++executions;
        });

    ThreadGroup group({std::move(task.package)});
    group.startAll();
    group.wait();

    EXPECT_THROW(group.startAll(), std::logic_error);
    EXPECT_EQ(task.future.get(), 1);
    EXPECT_EQ(executions.load(), 1);
}

// 测试思路：wait() 在未启动状态下没有可回收线程，应明确报告
// 生命周期错误，而不是打印日志后伪装成成功。
TEST(TestThreadGroup, WaitBeforeStartIsRejected)
{
    auto task = ThreadGroup::MakePackage(
        "not-started",
        [](const ThreadGroup::StopPredicate&) -> int {
            return 1;
        });

    ThreadGroup group({std::move(task.package)});

    EXPECT_THROW(group.wait(), std::logic_error);
    EXPECT_FALSE(group.joined());
}

// 测试思路：join 是幂等的。第一次 wait() 回收线程，第二次 wait()
// 不应再次 join，也不应改变 future 的结果。
TEST(TestThreadGroup, WaitIsIdempotentAfterJoin)
{
    auto task = ThreadGroup::MakePackage(
        "idempotent-wait",
        [](const ThreadGroup::StopPredicate&) -> std::string {
            return "done";
        });

    ThreadGroup group({std::move(task.package)});
    group.startAll();
    group.wait();
    ASSERT_TRUE(group.joined());

    EXPECT_NO_THROW(group.wait());
    EXPECT_EQ(task.future.get(), "done");
}

// 测试思路：停止请求可以在 startAll() 之前发布。线程启动后应能
// 立即观察到已设置的停止标志，而不是把“未启动”误认为“不可停止”。
TEST(TestThreadGroup, StopRequestedBeforeStartIsVisibleToTask)
{
    auto task = ThreadGroup::MakePackage(
        "pre-stopped",
        [](const ThreadGroup::StopPredicate& stop) -> bool {
            return stop();
        });

    ThreadGroup group({std::move(task.package)});
    group.requestStop();
    group.startAll();
    group.wait();

    ASSERT_TRUE(task.future.get());
}

// 测试思路：future 不要求业务结果可复制。返回包含 unique_ptr 的
// move-only 类型，验证 packaged_task/future 的结果不会退化为 any。
TEST(TestThreadGroup, MoveOnlyBusinessResultIsPreserved)
{
    struct MoveOnlyResult {
        std::unique_ptr<int> value;

        MoveOnlyResult() = default;
        explicit MoveOnlyResult(int number)
            : value(std::make_unique<int>(number)) {}

        MoveOnlyResult(MoveOnlyResult&&) noexcept = default;
        MoveOnlyResult& operator=(MoveOnlyResult&&) noexcept = default;
        MoveOnlyResult(const MoveOnlyResult&) = delete;
        MoveOnlyResult& operator=(const MoveOnlyResult&) = delete;
    };

    auto task = ThreadGroup::MakePackage(
        "move-only-result",
        [](const ThreadGroup::StopPredicate&) -> MoveOnlyResult {
            return MoveOnlyResult(42);
        });

    ThreadGroup group({std::move(task.package)});
    group.startAll();
    group.wait();

    auto result = task.future.get();
    ASSERT_TRUE(result.value);
    EXPECT_EQ(*result.value, 42);
}

// 测试思路：任务名是线程诊断和后续结果关联的基础标识。构造时
// 传入空名称应立即失败，而不是等到线程启动后才暴露问题。
TEST(TestThreadGroup, EmptyPackageNameIsRejected)
{
    auto task = ThreadGroup::MakePackage(
        "",
        [](const ThreadGroup::StopPredicate&) -> int {
            return 1;
        });

    EXPECT_THROW(
        ThreadGroup group({std::move(task.package)}),
        std::invalid_argument);
}
