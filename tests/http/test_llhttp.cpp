/**
 * @file test_llhttp.cpp
 * @brief llhttp库使用测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 05:14:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "../test_log.h"
#include <gtest/gtest.h>
#include <llhttp.h>
#include <unordered_map>
#include <string>
#include <iostream>

static const char g_test_req[] = \
"POST /index.html?name=test_name HTTP/1.1\r\n" \
"Host: www.chenshuo.com\r\n" \
"User-Agent: kit_muduo\r\n";

static const char g_test_req2[] = \
"Content-Length: 20\r\n" \
"Accept-Encoding: UTF-8\r\n" \
"\r\ntest body 1234567890";


int32_t handle_on_message_complete(llhttp_t *parser) {
  TEST_INFO() << "Message completed!" << std::endl;
  return 0;
}

TEST(Testllhttp, test)
{
    llhttp_t parser;
    llhttp_settings_t settings;

    struct HeaderContext {
        std::string cur_header;
        std::unordered_map<std::string, std::string> headers;
    };

    /*Initialize user callbacks and settings */
    llhttp_settings_init(&settings);

    /*Set user callback */
    // mehtod 解析
    settings.on_method = [](llhttp_t* parser, const char *data, size_t len) -> int {
        TEST_INFO() << "method: " << std::string(data, len) << std::endl;
        return 0;
    };
    // url 解析
    settings.on_url = [](llhttp_t* parser, const char *data, size_t len) -> int {
        TEST_INFO() << "url: " << std::string(data, len) << std::endl;
        return 0;
    };
    // 协议版本解析
    settings.on_version = [](llhttp_t* parser, const char *data, size_t len) -> int {
        TEST_INFO() << "version: " << std::string(data, len) << std::endl;
        return 0;
    };

    // 头部字段解析
    settings.on_header_field = [](llhttp_t* parser, const char *data, size_t len) -> int {
        auto ctx = static_cast<HeaderContext*>(parser->data);
        ctx->cur_header.assign(data, len);
        return 0;
    };
    settings.on_header_value = [](llhttp_t* parser, const char *data, size_t len) -> int {
        auto ctx = static_cast<HeaderContext*>(parser->data);
        if(!ctx->cur_header.empty())
        {
            ctx->headers[ctx->cur_header] += std::string(data, len);
        }
        return 0;
    };

    settings.on_headers_complete = [](llhttp_t* parser) -> int {
        auto ctx = static_cast<HeaderContext*>(parser->data);
        std::cout << "===================\n";
        for(auto &it : ctx->headers)
        {
            TEST_INFO() << it.first << " : " << it.second << std::endl;
        }
        std::cout << "===================\n";

        return 0;
    };
    // body解析
    settings.on_body = [](llhttp_t* parser, const char *data, size_t len) -> int {
        TEST_INFO() << "body: " << std::string(data, len) << std::endl;

        return 0;
    };


    // 必须使用该回调, 目的是处理可能存在的分段报文, 当收到完整报文时才能进行下一步
    settings.on_message_complete = [](llhttp_t* handle) -> int {
        TEST_INFO() << "Message completed!" << std::endl;
        return 0;
    };

    /*Initialize the parser in HTTP_BOTH mode, meaning that it will select between
    *HTTP_REQUEST and HTTP_RESPONSE parsing automatically while reading the first
    *input.
    */
    llhttp_init(&parser, HTTP_BOTH, &settings);

    HeaderContext ctx;
    parser.data = (void*)&ctx;

    const char *data[2] = {0};
    data[0] = g_test_req;
    data[1] = g_test_req2;

    /*Parse request! */
    // 支持分段解析
    for(int i = 0;i < 2;++i)
    {
        const char *request = data[i];

        int request_len = strlen(request);

        llhttp_errno err = llhttp_execute(&parser, request, request_len);
        if (err == HPE_OK)
        {
            TEST_INFO() << "Successfully parsed!\n";
        }
        else
        {
            TEST_ERROR() << "Parse error: " << llhttp_errno_name(err) << ", "
                << llhttp_get_error_reason(&parser)
                << ", err pos:" << "`" <<llhttp_get_error_pos(&parser) << "`"
                << std::endl;
        }
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}