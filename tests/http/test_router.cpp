/**
 * @file test_router.cpp
 * @brief 路由匹配处理
 * @author ljk5
 * @version 1.0
 * @date 2025-08-14 17:14:32
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "gtest/gtest.h"
#include "../test_log.h"

#include <regex>


TEST(TestRouter, regex)
{
    std::string ori_pattern = "/projects/query/:id/:other";
    auto regex_pattern = std::regex_replace(ori_pattern, std::regex(":[^/]+"), "([^/]+)");
    // auto regex_pattern = std::regex_replace(ori_pattern, std::regex(":[^/]+"), R"((\d+))");

    regex_pattern = "^" + regex_pattern + "$";

    TEST_INFO() << ori_pattern << " --> " << regex_pattern << std::endl;
    std::regex _regex(regex_pattern);

    std::smatch matches;
    std::string url = "/projects/query/1/6";
    std::regex param_rex(":([^/]+)");
    // std::regex param_rex(":([^/(0-9)]+)");
    
    if (std::regex_match(url, matches, _regex)) 
    {
        // 提取动态参数
        auto param_it = std::sregex_iterator(ori_pattern.begin(), ori_pattern.end(), param_rex);
        auto match_it = ++matches.begin();
        
        TEST_INFO() << ori_pattern << " --> " << regex_pattern << std::endl;

        while(param_it != std::sregex_iterator() && match_it != matches.end())
        {
            std::cout << (*param_it)[1].str() << " : " << match_it->str() << std::endl;
            ++param_it;
            ++match_it;
        }
    }
    else
    {
        TEST_INFO() << "no match" << std::endl;
    }

}



int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}