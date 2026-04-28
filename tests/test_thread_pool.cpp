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
#include <string.h>

using namespace kit_muduo;


static std::string test_func(int a)
{
    TEST_DEBUG() << "test_func, a= " << a << std::endl;
    sleep(5);

    return std::string("future success" + std::to_string(a));
}


TEST(TestThreadPool, test)
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

    TEST_INFO() << "f1 restult= " << f1.get() << std::endl;
    TEST_INFO() << "pool stop" << std::endl;
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}