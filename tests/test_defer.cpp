#include "base/defer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

using kit_muduo::Defer;

// 测试思路：构造时不执行，离开作用域后只执行一次。示例：计数 0 -> 1。
TEST(DeferTest, RunsOnceAtScopeExit)
{
    int calls = 0;
    {
        Defer cleanup([&calls]() noexcept { ++calls; });
        EXPECT_EQ(calls, 0);
        static_assert(!std::is_copy_constructible_v<decltype(cleanup)>);
        static_assert(!std::is_copy_assignable_v<decltype(cleanup)>);
        static_assert(!std::is_move_constructible_v<decltype(cleanup)>);
        static_assert(!std::is_move_assignable_v<decltype(cleanup)>);
        static_assert(std::is_nothrow_destructible_v<decltype(cleanup)>);
    }
    EXPECT_EQ(calls, 1);
}

// 测试思路：提前 return 仍执行清理，且不改变返回值。示例：返回 7，计数变为 1。
TEST(DeferTest, RunsOnEarlyReturn)
{
    int calls = 0;
    const auto run = [&calls]() {
        Defer cleanup([&calls]() noexcept { ++calls; });
        return 7;
    };
    EXPECT_EQ(run(), 7);
    EXPECT_EQ(calls, 1);
}

// 测试思路：有处理器的异常展开经过局部对象时执行清理。示例：抛异常后计数为 1。
TEST(DeferTest, RunsDuringExceptionUnwinding)
{
    int calls = 0;
    const auto run = [&calls]() {
        Defer cleanup([&calls]() noexcept { ++calls; });
        throw std::runtime_error("unwind");
    };
    EXPECT_THROW(run(), std::runtime_error);
    EXPECT_EQ(calls, 1);
}

// 测试思路：嵌套块先清理，同一块中后声明的先执行。示例：注册 1、2、内层 3 -> 3、2、1。
TEST(DeferTest, RunsInReverseOrderWithinEachScope)
{
    std::vector<int> order;
    {
        Defer first([&order]() { order.push_back(1); });
        Defer second([&order]() { order.push_back(2); });
        {
            Defer inner([&order]() { order.push_back(3); });
            EXPECT_TRUE(order.empty());
        }
        EXPECT_EQ(order, (std::vector<int>{3}));
    }
    EXPECT_EQ(order, (std::vector<int>{3, 2, 1}));
}

// 测试思路：保存只可移动的 mutable lambda 并执行。示例：捕获 unique_ptr(42)，退出时读出并释放。
TEST(DeferTest, SupportsMoveOnlyMutableCallback)
{
    int value = 0;
    {
        Defer cleanup([resource = std::make_unique<int>(42), &value]() mutable noexcept {
            value = *resource;
            resource.reset();
        });
        EXPECT_EQ(value, 0);
    }
    EXPECT_EQ(value, 42);
}

// 测试思路：左值回调可注册，捕获方式决定读取时机。示例：源值 1 -> 2，值捕获得 1，引用捕获得 2。
TEST(DeferTest, PreservesValueAndReferenceCaptureSemantics)
{
    int source = 1;
    int captured_value = 0;
    int captured_reference = 0;
    {
        auto callback = [source, &captured_value]() noexcept { captured_value = source; };
        Defer by_value(callback);
        Defer by_reference([&source, &captured_reference]() noexcept {
            captured_reference = source;
        });
        source = 2;
    }
    EXPECT_EQ(captured_value, 1);
    EXPECT_EQ(captured_reference, 2);
}

/*
测试思路：continue 和 break 都必须清理当前迭代的局部对象，下一次迭代
开始前应已完成上一次清理；循环外的 Defer 则继续存活。
示例：第 0 次 continue -> 清理 0；第 1 次 break -> 清理 1；最后清理外层。
*/
TEST(DeferTest, CleansEachIterationOnContinueAndBreak)
{
    std::array<int, 2> order{-1, -1};
    int count = 0;
    bool outer_cleaned = false;
    {
        Defer outer([&outer_cleaned]() noexcept { outer_cleaned = true; });
        for(int i = 0; i < 3; ++i)
        {
            EXPECT_EQ(count, i);
            Defer iteration([i, &order, &count]() noexcept { order[count++] = i; });
            if(i == 0)
            {
                continue;
            }
            break;
        }
        EXPECT_EQ(count, 2);
        EXPECT_EQ(order, (std::array<int, 2>{0, 1}));
        EXPECT_FALSE(outer_cleaned);
    }
    EXPECT_TRUE(outer_cleaned);
}

/*
测试思路：异常跨越内外两层作用域时，每个回调仍按逆序执行一次，原异常
继续传播到调用方。使用定长数组，避免清理回调动态分配内存。
示例：外层注册 1、2，内层注册 3 后抛出整数 7 -> 清理 3、2、1，捕获到 7。
*/
TEST(DeferTest, UnwindsNestedScopesInReverseOrder)
{
    std::array<int, 3> order{};
    int count = 0;
    try
    {
        Defer first([&order, &count]() noexcept { order[count++] = 1; });
        Defer second([&order, &count]() noexcept { order[count++] = 2; });
        {
            Defer inner([&order, &count]() noexcept { order[count++] = 3; });
            throw 7;
        }
    }
    catch(int value)
    {
        EXPECT_EQ(value, 7);
        EXPECT_EQ(count, 3);
    }
    EXPECT_EQ(count, 3);
    EXPECT_EQ(order, (std::array<int, 3>{3, 2, 1}));
}

/*
测试思路：回调拥有的资源在执行期间仍有效，回调执行完且 Defer 析构后释放。
用 weak_ptr 观察真实生命周期，而非只检查回调有没有被调用。
示例：唯一 shared_ptr 移入回调 -> 执行时读出 42 -> 退出后 weak_ptr 过期。
*/
TEST(DeferTest, KeepsCapturedResourceAliveUntilCleanupCompletes)
{
    auto resource = std::make_shared<int>(42);
    std::weak_ptr<int> observer = resource;
    int value = 0;
    {
        Defer cleanup([resource = std::move(resource), &value]() noexcept {
            value = *resource;
        });
        EXPECT_FALSE(resource);
        EXPECT_FALSE(observer.expired());
        EXPECT_EQ(value, 0);
    }
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(observer.expired());
}

/*
测试思路：违反回调不得向外抛异常的约定时，noexcept 析构必须调用 terminate。
在 GTest 子进程设置 terminate handler，以专用退出码区分正常返回或异常传播，
不修改主测试进程的 handler，也不依赖运行库错误文案或生成 core 文件。
示例：清理回调抛 runtime_error -> terminate handler -> 子进程退出码 73。
*/
TEST(DeferDeathTest, ThrowingCallbackTerminates)
{
    EXPECT_EXIT(
        {
            std::set_terminate([]() { std::_Exit(73); });
            {
                Defer cleanup([]() { throw std::runtime_error("cleanup failed"); });
            }
            std::_Exit(0);
        },
        testing::ExitedWithCode(73), "");
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
