/**
 * @file test_http.cpp
 * @brief
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 20:31:28
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "../test_log.h"
#include "net/http/http_request.h"
#include "net/http/http_context.h"
#include "base/time_stamp.h"
#include "net/buffer.h"

#include <gtest/gtest.h>

using namespace kit_muduo;
using namespace kit_muduo::http;

static const char g_test_req[] = "GET /index.html HTTP/1.1\r\n"
"Host: www.chenshuo.com\r\n"
"User-Agent: kit_muduo\r\n"
"Accept-Encoding: UTF-8\r\n"
"\r\n561wefwe65f1ewf";


TEST(TestHttp, http_requst)
{
    HttpContext context;
    Buffer buf;
    buf.append(g_test_req, sizeof(g_test_req));
    EXPECT_EQ(buf.readableBytes(), sizeof(g_test_req));

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req.method().toString(), "GET");
    EXPECT_STREQ(req.path().c_str(), "/index.html");
    EXPECT_STREQ(req.version().toString(), "HTTP/1.1");
    auto it = req.headers().find("Host");
    EXPECT_TRUE(it != req.headers().end());
    EXPECT_STREQ(it->first.c_str(), "Host");
    EXPECT_STREQ(it->second.c_str(), "www.chenshuo.com");

    it = req.headers().find("User-Agent");
    EXPECT_TRUE(it != req.headers().end());
    EXPECT_STREQ(it->first.c_str(), "User-Agent");
    EXPECT_STREQ(it->second.c_str(), "kit_muduo");

    it = req.headers().find("Accept-Encoding");
    EXPECT_TRUE(it != req.headers().end());
    EXPECT_STREQ(it->first.c_str(), "Accept-Encoding");
    EXPECT_STREQ(it->second.c_str(), "UTF-8");

    EXPECT_EQ(now.millSeconds(), req.receiveTime().millSeconds());

    TEST_INFO() << "Body: "<< "|" << buf.peek() << "|" << std::endl;
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}