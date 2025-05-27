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

#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestTimer, test)
{
    Timer timer([](){
        TEST_DEBUG() << "cur time= " << TimeStamp::Now().toString() << ", timer is expired! \n" << std::endl;
    }, TimeStamp::Now(), 5);
    TEST_DEBUG() << "cur time= " << TimeStamp::Now().toString() << std::endl;
    sleep(6);
    if(TimeStamp::Now() >= timer.expiration())
        timer.run();
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}