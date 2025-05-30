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
#include "net/http/http_response.h"
#include "net/http/http_server.h"
#include "net/event_loop.h"

#include <gtest/gtest.h>

using namespace kit_muduo;
using namespace kit_muduo::http;

static const char g_test_req[] = "GET /index.html HTTP/1.1\r\n"
"Host: www.chenshuo.com\r\n"
"User-Agent: kit_muduo\r\n"
"Accept-Encoding: UTF-8\r\n"
"\r\n561wefwe65f1ewf";


TEST(TestHttpReq, raw_data)
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

    TEST_INFO() << "Body: "<< "|" << req.body() << "|" << std::endl;
}


TEST(TestHttpReq, create_data)
{
    HttpRequet orireq;

    orireq.setMethod(HttpRequet::Method::kGet);
    orireq.setPath("www.kit.com/index");
    orireq.setVersion(Version::kHttp11);
    orireq.addHeader("Host", "www.kit.com");
    orireq.addHeader("XData", "666");
    orireq.appendBody("12345678", strlen("12345678"));

    std::cout << orireq.toString() << std::endl;
    std::cout << "---------------------\n";

    Buffer buf;
    buf.append(orireq.toString().c_str(), orireq.toString().size());
    EXPECT_EQ(buf.readableBytes(), orireq.toString().size());

    HttpContext context;

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req.method().toString(), "GET");
    EXPECT_STREQ(req.path().c_str(), "www.kit.com/index");
    EXPECT_STREQ(req.version().toString(), "HTTP/1.1");
    auto it = req.headers().find("Host");
    EXPECT_TRUE(it != req.headers().end());
    EXPECT_STREQ(it->first.c_str(), "Host");
    EXPECT_STREQ(it->second.c_str(), "www.kit.com");

    it = req.headers().find("XData");
    EXPECT_TRUE(it != req.headers().end());
    EXPECT_STREQ(it->first.c_str(), "XData");
    EXPECT_STREQ(it->second.c_str(), "666");

    EXPECT_EQ(now.millSeconds(), req.receiveTime().millSeconds());
    EXPECT_STREQ(req.body().c_str(), "12345678");

    TEST_INFO() << "Body: "<< "|" << req.body() << "|" << std::endl;
}


TEST(TestHttpResp, create_data)
{
    HttpResponse resp;
    resp.setVersion(Version::kHttp11);
    resp.setStateCode(HttpResponse::StateCode::k301MovedPermanently);
    resp.addHeader("XData", "999");
    resp.appendBody("99999999", strlen("99999999"));
    std::cout << resp.toString() << std::endl;
    std::cout << "----------------\n";


}
void testHttpCb(const HttpRequet &req, HttpResponse &resp)
{
    TEST_INFO() << "req body= " << req.body() << std::endl;
    resp.setStateCode(HttpResponse::StateCode::k200Ok);
    resp.setVersion(Version::kHttp11);
    resp.setConnectionClosed(true);

    resp.appendBody(req.body());
    resp.appendBody("\n");
}


TEST(TestHttpServer, test)
{
    EventLoop loop;
    InetAddress addr(5555);
    HttpServer server(&loop, addr, "myhttp", TcpServer::KReusetPort);
    server.setThreadNum(1);

    server.setHttpCallback(testHttpCb);

    server.start();
    loop.loop();
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}