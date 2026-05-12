/**
 * @file test_runtime_result.cpp
 * @brief runtime 操作结果与同步 loop 调用辅助函数测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-09
 */

#include "base/event_loop_thread.h"
#include "domain/runtime_result.h"
#include "net/event_loop.h"

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>

using namespace kit_domain;
using namespace kit_muduo;

namespace {

constexpr int64_t kDefaultTimeoutMs = 1000;
constexpr auto kTestWaitTimeout = std::chrono::seconds(2);

EventLoopThread MakeRuntimeLoopThread()
{
    return EventLoopThread(nullptr, "runtime_result_test_loop");
}

RuntimeResult<int> OkInt(int val)
{
    RuntimeResult<int> result;
    result.val = val;
    return result;
}

RuntimeResult<void> OkVoid()
{
    return RuntimeResult<void>();
}

} // namespace

/**
 * 测试思路：
 *   InvokeOnLoopSync(nullptr, timeout, func)
 *        |
 *        v
 *   参数校验在投递任务前失败
 *        |
 *        v
 *   RuntimeResult.ok()==false，并给出明确 RuntimeError
 *
 * 示例：业务 handler 查询不到 project runtime，传入的 loop 为空。此时不能继续
 * 调用 queueInLoop，否则会空指针崩溃；期望结果是 ok()==false，错误码是
 * kInvokeInvalidArgument。
 */
TEST(TestRuntimeResult, NullLoopReturnsFailedResult)
{
    auto result = InvokeOnLoopSync(nullptr, kDefaultTimeoutMs, [] {
        return OkInt(1);
    });

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.val, 0);
    EXPECT_EQ(result.error.toInt(), RuntimeError::kInvokeInvalidArgument);
}

/**
 * 测试思路：
 *   测试线程调用 InvokeOnLoopSync(loop, timeout, func)
 *        |
 *        v
 *   helper 将 func 投递到 runtime loop
 *        |
 *        v
 *   测试线程同步等待 future，并拿到 func 返回值
 *
 * 示例：模拟 Web 线程把 AddProtocolItem 投递到项目 runtime loop。func 内部检查
 * 当前线程确实是 loop 线程，并返回 42；期望 RuntimeResult.ok()==true、val=42。
 */
TEST(TestRuntimeResult, CrossThreadInvokeReturnsValueFromLoopThread)
{
    auto loop_thread = MakeRuntimeLoopThread();
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    auto result = InvokeOnLoopSync(loop, kDefaultTimeoutMs, [loop] {
        return OkInt(loop->isInLoopThread() ? 42 : -1);
    });

    EXPECT_TRUE(result.ok()) << result.error.toMsg();
    EXPECT_EQ(result.val, 42);
    EXPECT_EQ(result.error.toInt(), RuntimeError::kOk);

    loop_thread.quit();
}

/**
 * 测试思路：
 *   runtime loop 线程内部再次调用 InvokeOnLoopSync(loop, timeout, func)
 *        |
 *        v
 *   helper 识别 isInLoopThread()==true
 *        |
 *        v
 *   直接执行 func，不再 queueInLoop 后等待自己，避免死锁
 *
 * 示例：后续 R3/R4 中 runtime 操作可能已经处在 loop 线程内。如果仍然投递并同步
 * 等待，就会出现自己等自己的死锁。本用例在 loop 回调中调用 helper，期望 2 秒内
 * 返回 ok=true、val=7。
 */
TEST(TestRuntimeResult, InvokeInsideLoopThreadRunsInlineWithoutDeadlock)
{
    auto loop_thread = MakeRuntimeLoopThread();
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    auto done = std::make_shared<std::promise<RuntimeResult<int>>>();
    auto future = done->get_future();

    loop->queueInLoop([loop, done] {
        done->set_value(InvokeOnLoopSync(loop, kDefaultTimeoutMs, [] {
            return OkInt(7);
        }));
    });

    ASSERT_EQ(future.wait_for(kTestWaitTimeout), std::future_status::ready)
        << "loop 线程内调用 InvokeOnLoopSync 不应发生同步等待死锁";
    auto result = future.get();
    EXPECT_TRUE(result.ok()) << result.error.toMsg();
    EXPECT_EQ(result.val, 7);

    loop_thread.quit();
}

/**
 * 测试思路：
 *   InvokeOnLoopSync 支持 void 返回值
 *        |
 *        v
 *   func 执行成功后没有 val 字段可取
 *        |
 *        v
 *   RuntimeResult<void>.ok()==true 表示运行态操作成功
 *
 * 示例：R3 以后 stop() 或某些只修改内部状态的 runtime 操作可能不需要返回业务值。
 * 本用例用 atomic 计数模拟副作用，期望 helper 能正确处理 void 函数并报告成功。
 */
TEST(TestRuntimeResult, VoidFunctionReturnsSuccessResult)
{
    auto loop_thread = MakeRuntimeLoopThread();
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::atomic<int> side_effect{0};
    auto result = InvokeOnLoopSync(loop, kDefaultTimeoutMs, [&side_effect] {
        side_effect.store(1);
        return OkVoid();
    });

    EXPECT_TRUE(result.ok()) << result.error.toMsg();
    EXPECT_EQ(side_effect.load(), 1);
    EXPECT_EQ(result.error.toInt(), RuntimeError::kOk);

    loop_thread.quit();
}

/**
 * 测试思路：
 *   runtime func 抛出异常
 *        |
 *        v
 *   packaged_task/future 将异常传回等待线程
 *        |
 *        v
 *   helper 捕获异常，转成 RuntimeResult.ok()==false 和 kInvokeException
 *
 * 示例：ProtocolItemFactory 或运行态更新函数内部抛出 "boom"。接口边界不能让异常
 * 穿透到 Web handler 外层导致请求处理崩溃；期望返回失败结果和稳定错误码。
 */
TEST(TestRuntimeResult, ExceptionIsConvertedToFailedResult)
{
    auto loop_thread = MakeRuntimeLoopThread();
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    auto result = InvokeOnLoopSync(loop, kDefaultTimeoutMs, []() -> RuntimeResult<int> {
        throw std::runtime_error("boom");
    });

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.val, 0);
    EXPECT_EQ(result.error.toInt(), RuntimeError::kInvokeException);

    loop_thread.quit();
}

/**
 * 测试思路：
 *   runtime loop 先执行一个阻塞回调
 *        |
 *        v
 *   测试线程再调用 InvokeOnLoopSync(loop, 20ms, func)
 *        |
 *        v
 *   func 无法在超时前被调度，helper 返回 timeout 失败
 *
 * 示例：运行态线程被长耗时协议处理占住，Web handler 同步等待 AddProtocolItem。
 * 如果超过约定时间，接口需要拿到 ok()==false，而不是无限等待。本用例先阻塞 loop，
 * 再用 20ms 超时验证错误码是 kInvokeTimeout。
 */
TEST(TestRuntimeResult, TimeoutReturnsFailedResultWhenLoopIsBusy)
{
    auto loop_thread = MakeRuntimeLoopThread();
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    auto blocker_started = std::make_shared<std::promise<void>>();
    auto release_blocker = std::make_shared<std::promise<void>>();
    auto release_future = release_blocker->get_future().share();

    loop->queueInLoop([blocker_started, release_future] {
        blocker_started->set_value();
        release_future.wait();
    });

    ASSERT_EQ(blocker_started->get_future().wait_for(kTestWaitTimeout), std::future_status::ready);

    auto result = InvokeOnLoopSync(loop, 20, [] {
        return OkInt(99);
    });

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.val, 0);
    EXPECT_EQ(result.error.toInt(), RuntimeError::kInvokeTimeout);

    release_blocker->set_value();
    loop_thread.quit();
}
