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

static const char g_test_req[] = \
"GET /index.html HTTP/1.1\r\n" \
"Host: www.chenshuo.com\r\n" \
"User-Agent: kit_muduo\r\n" \
"Content-Length: 15\r\n" \
"Accept-Encoding: UTF-8\r\n" \
"\r\n561wefwe65f1ewf";

static const char g_test_resp[] = \
"HTTP/1.1 200 OK\r\n" \
"Host: www.chenshuo.com\r\n" \
"Content-Length: 6\r\n" \
"Accept-Encoding: UTF-8\r\n" \
"\r\n123456";


TEST(TestHttpReq, raw_data)
{
    HttpContext context;
    Buffer buf;
    buf.append(g_test_req, strlen(g_test_req));
    EXPECT_EQ(buf.readableBytes(), strlen(g_test_req));

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req->method().toString(), "GET");
    EXPECT_STREQ(req->path().c_str(), "/index.html");
    EXPECT_STREQ(req->version().toString(), "HTTP/1.1");
    auto it = req->headers().find("Host");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Host");
    EXPECT_STREQ(it->second.c_str(), "www.chenshuo.com");

    it = req->headers().find("User-Agent");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "User-Agent");
    EXPECT_STREQ(it->second.c_str(), "kit_muduo");

    it = req->headers().find("Accept-Encoding");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Accept-Encoding");
    EXPECT_STREQ(it->second.c_str(), "UTF-8");

    it = req->headers().find("Content-Length");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Content-Length");
    EXPECT_STREQ(it->second.c_str(), "15");

    EXPECT_EQ(now.millSeconds(), req->receiveTime().millSeconds());

    TEST_INFO() << "Body: "<< "|" << req->body().toString() << "|" << std::endl;
}

TEST(TestHttpReq, query_params)
{
    static const char test_req[] = \
    "GET /projects?project_id=42&name=kit+muduo&empty=&encoded=a%2Bb%20c&flag HTTP/1.1\r\n" \
    "Host: localhost\r\n" \
    "\r\n";

    HttpContext context;
    Buffer buf;
    buf.append(test_req, strlen(test_req));

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req->path().c_str(), "/projects");
    EXPECT_STREQ(req->getQureyParam("project_id").c_str(), "42");
    EXPECT_STREQ(req->getQureyParam("name").c_str(), "kit muduo");
    EXPECT_STREQ(req->getQureyParam("empty").c_str(), "");
    EXPECT_STREQ(req->getQureyParam("encoded").c_str(), "a+b c");
    EXPECT_STREQ(req->getQureyParam("flag").c_str(), "");
}

TEST(TestHttpReq, query_params_segmented_url)
{
    HttpContext context;
    auto now = TimeStamp::Now();

    EXPECT_EQ(context.parseRequest("GET /pro", now), true);
    EXPECT_EQ(context.gotAll(), false);

    bool ok = context.parseRequest("jects?project_id=42&name=kit+muduo HTTP/1.1\r\n"
                                   "Host: localhost\r\n"
                                   "\r\n", now);

    EXPECT_EQ(ok, true);
    EXPECT_EQ(context.gotAll(), true);
    auto req = context.request();
    EXPECT_STREQ(req->path().c_str(), "/projects");
    EXPECT_STREQ(req->getQureyParam("project_id").c_str(), "42");
    EXPECT_STREQ(req->getQureyParam("name").c_str(), "kit muduo");
}


TEST(TestHttpReq, create_data)
{
    HttpRequest orireq;

    orireq.setMethod(HttpRequest::Method::kGet);
    orireq.setPath("/main.html");
    orireq.setVersion(Version::kHttp11);
    orireq.addHeader("Host", "www.kit.com");
    orireq.addHeader("XData", "666");
    std::string body = "12345678";
    orireq.body().appendData(body);
    orireq.addHeader("Content-Length", std::to_string(body.size()));


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
    EXPECT_STREQ(req->method().toString(), "GET");
    EXPECT_STREQ(req->path().c_str(), "/main.html");
    EXPECT_STREQ(req->version().toString(), "HTTP/1.1");
    auto it = req->headers().find("Host");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Host");
    EXPECT_STREQ(it->second.c_str(), "www.kit.com");

    it = req->headers().find("XData");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "XData");
    EXPECT_STREQ(it->second.c_str(), "666");

    EXPECT_EQ(now.millSeconds(), req->receiveTime().millSeconds());
    EXPECT_STREQ(req->body().toString().c_str(), "12345678");

    TEST_INFO() << "Body: "<< "|" << req->body().toString() << "|" << std::endl;
}


void testHttpCb(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    TEST_INFO() << "req body= " << req->body().toString() << std::endl;
    resp->setStateCode(StateCode::k200Ok);
    resp->setVersion(Version::kHttp11);
    resp->setConnectionClosed(true);

    resp->body().appendData(req->body().data());
    resp->body().appendData("\n");
    conn->send(resp->toString());

}

TEST(TestHttpResp, create_data)
{
    HttpResponse oriresp;
    oriresp.setVersion(Version::kHttp11);
    oriresp.setStateCode(StateCode::k200Ok);
    oriresp.addHeader("XData", "999");
    oriresp.body().appendData("123456", strlen("123456"));
    std::cout << oriresp.toString() << std::endl;
    std::cout << "----------------\n";


    HttpContext context;

    auto now = TimeStamp::Now();
    bool ok = context.parseResponse(oriresp.toString(), now);

    EXPECT_EQ(ok, true);
    auto resp = context.response();
    EXPECT_STREQ(resp->stateCode().toString().c_str(), "200");
    EXPECT_STREQ(resp->version().toString(), "HTTP/1.1");
    auto it = resp->headers().find("XData");
    EXPECT_TRUE(it != resp->headers().end());
    EXPECT_STREQ(it->first.c_str(), "XData");
    EXPECT_STREQ(it->second.c_str(), "999");

    EXPECT_STREQ(resp->body().toString().c_str(), "123456");

    TEST_INFO() << "Body: "<< "|" << resp->body().toString() << "|" << std::endl;


}

TEST(TestHttpServer, DISABLED_listen)
{
    EventLoop loop;
    InetAddress addr(5555, "192.168.77.136");
    HttpServer server(&loop, addr, "myhttp", TcpServer::KReusePort);
    server.setThreadNum(1);
    server.setHttpCallback(testHttpCb);

    server.start();
    loop.loop();
}


TEST(TestHttpServer, DISABLED_servlet)
{
    EventLoop loop;
    InetAddress addr(5555, "192.168.77.136");
    HttpServer server(&loop, addr, "http-servlet", TcpServer::KReusePort);
    server.setThreadNum(1);

    auto sd = server.getServletDispatch();

    // 添加固定的服务
    sd->addServlet("/hello", std::make_shared<HelloServlet>("kit_proxy_server/1.0.0"));

    // 自定义函数形式
    sd->addServlet("/custom", [](TcpConnectionPtr conn,  HttpContextPtr ctx) {

        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);
        resp->setStateCode(StateCode::k200Ok);
        resp->addHeader("Content-Type", "text/plaint");

        std::string body = "this is a custom servlet!!";
        resp->body().appendData(body);
    });

    server.setHttpCallback([dispatch = sd](TcpConnectionPtr conn, HttpContextPtr ctx){
        auto req = ctx->request();
        auto resp = ctx->response();

        const std::string &connection = req->getHeader("Connection");
        bool closed = (connection == "close")
                || (Version::kHttp10 == req->version()() && connection != "keep-alive");


        resp->setConnectionClosed(closed);

        ////////////这部分可以异步//////////////
        dispatch->handle(conn, ctx);

        conn->send(req->toString());
        if(resp->connectionClosed())
        {
            conn->shutdown();
        }
        ////////////这部分可以异步//////////////
    });

    server.start();
    loop.loop();
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
