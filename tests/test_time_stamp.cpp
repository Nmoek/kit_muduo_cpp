/**
 * @file test_time_stamp.cpp
 * @brief  时间通用类测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 21:44:39
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/time_stamp.h"
#include "./test_log.h"

#include <gtest/gtest.h>

using namespace kit_muduo;


TEST(TestTimeStamp, test)
{

    TEST_INFO() << "now time stamp: " << TimeStamp::Now().toString() << std::endl;

    TEST_INFO() << "now: " << TimeStamp::NowTimeStamp() << std::endl;
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}