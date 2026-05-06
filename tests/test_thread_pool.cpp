/**
 * @file test_thread_pool.cpp
 * @brief 工作线程池测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-07-06 21:50:30
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/thread_pool.h"
#include "./test_log.h"

#include "gtest/gtest.h"
#include <future>
#include <unistd.h>

using namespace kit_muduo;


static std::string test_func(int a)
{
    TEST_DEBUG() << "test_func, a= " << a << std::endl;
    sleep(5);

    return std::string("future success" + std::to_string(a));
}


TEST(TestThreadPool, SubmitTask)
{
    ThreadPool pool(4);
    pool.setMode(ThreadPool::CACHE_MOD);
    pool.setThreadMaxThreshHold(10);
    pool.setTaskQueMaxThreshHold(2);
    pool.setThreadMaxIdleInterval(2);
    pool.start();

    auto f1 = pool.submitTask(test_func, 1);

    pool.submitTask(test_func, 2);
    pool.submitTask(test_func, 3);
    pool.submitTask(test_func, 4);
    pool.submitTask(test_func, 5);
    pool.submitTask(test_func, 6);
    pool.submitTask(test_func, 7);
    pool.submitTask(test_func, 8);
    pool.submitTask(test_func, 9);
    pool.submitTask(test_func, 10);
    pool.submitTask(test_func, 11);
    pool.submitTask(test_func, 12);

    TEST_INFO() << "f1 restult= " << f1.result_future.get() << std::endl;
    TEST_INFO() << "pool stop" << std::endl;
}


TEST(TestThreadPool, TrySubmitTask)
{
    ThreadPool pool(4);
    pool.setMode(ThreadPool::FIXED_MOD);
    // 连续放6个任务 4个被执行+1个在队列+1个超时返回
    pool.setTaskQueMaxThreshHold(1);
    pool.start();

    auto r1 = pool.trySubmitTask(200, test_func, 1);
    ASSERT_EQ(r1.status, ThreadPool::SubmitStatus::kOK);

    auto r2 = pool.trySubmitTask(200, test_func, 2);
    ASSERT_EQ(r2.status, ThreadPool::SubmitStatus::kOK);

    auto r3 = pool.trySubmitTask(200, test_func, 3);
    ASSERT_EQ(r3.status, ThreadPool::SubmitStatus::kOK);

    auto r4 = pool.trySubmitTask(200, test_func, 4);
    ASSERT_EQ(r4.status, ThreadPool::SubmitStatus::kOK);

    auto r5 = pool.trySubmitTask(200, test_func, 5);
    ASSERT_EQ(r5.status, ThreadPool::SubmitStatus::kOK);

    auto r6 = pool.trySubmitTask(200, test_func, 6);
    ASSERT_EQ(r6.status, ThreadPool::SubmitStatus::kTimeout);

    ASSERT_EQ(r1.result_future.get(), "future success1");
    ASSERT_EQ(r2.result_future.get(), "future success2");
    ASSERT_EQ(r3.result_future.get(), "future success3");
    ASSERT_EQ(r4.result_future.get(), "future success4");
    ASSERT_EQ(r5.result_future.get(), "future success5");
    
    pool.stop();

    auto r7 = pool.trySubmitTask(200, test_func, 7);
    ASSERT_EQ(r7.status, ThreadPool::SubmitStatus::kStopping);

}


TEST(TestThreadPool, CacheWorkerIdleExitDoesNotBreakPool)
{
    ThreadPool pool(1);
    pool.setMode(ThreadPool::CACHE_MOD);
    pool.setThreadMaxThreshHold(3);
    pool.setTaskQueMaxThreshHold(1);
    pool.setThreadMaxIdleInterval(1);
    pool.start();

    std::promise<int32_t> p1;
    auto f1 = p1.get_future();
    std::promise<int32_t> p2;
    auto f2 = p2.get_future();

    auto r1 = pool.submitTask([f = std::move(f1)]() mutable{
        EXPECT_EQ(f.get(), 1);
    });
    ASSERT_EQ(r1.status, ThreadPool::SubmitStatus::kOK);

    auto r2 = pool.submitTask([f = std::move(f2)]() mutable{
        EXPECT_EQ(f.get(), 2);
    });
    ASSERT_EQ(r2.status, ThreadPool::SubmitStatus::kOK);
    p1.set_value(1);
    p2.set_value(2);
    ASSERT_NO_THROW(r1.result_future.get());
    ASSERT_NO_THROW(r2.result_future.get());

    sleep(2);
    std::promise<int32_t> p3;
    auto f3 = p3.get_future();
    auto r3 = pool.submitTask([f = std::move(f3)]() mutable {
        EXPECT_EQ(f.get(), 3);
    });
    ASSERT_EQ(r3.status, ThreadPool::SubmitStatus::kOK);
    p3.set_value(3);
    ASSERT_NO_THROW(r3.result_future.get());

    pool.stop();
    auto r4 = pool.submitTask([](){
        TEST_DEBUG() << "hello \n";
    });
    ASSERT_EQ(r4.status, ThreadPool::SubmitStatus::kStopping);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}