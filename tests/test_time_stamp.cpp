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
    std::string time_str = TimeStamp::Now().toString();
    TEST_INFO() << "stamp --> str: " << time_str << std::endl;

    TEST_INFO() << "now: " << TimeStamp::NowMs() << std::endl;

    TEST_INFO() << "str --> stamp: " << TimeStamp::Str2TimeStamp(time_str) << std::endl;
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}