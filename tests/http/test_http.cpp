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
#include "net/http/http_util.h"
#include "base/event_loop_thread.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace {

struct FdGuard
{
    explicit FdGuard(int32_t input_fd = -1)
        :fd(input_fd)
    {}

    ~FdGuard()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
    }

    int32_t fd;
};

struct PickPortResult
{
    bool ok{false};
    uint16_t port{0};
    std::string error;
};

PickPortResult PickUnusedLoopbackPort()
{
    PickPortResult result;
    FdGuard listen_fd(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if(listen_fd.fd < 0)
    {
        result.error = "create socket failed: ";
        result.error += std::strerror(errno);
        return result;
    }

    int32_t on = 1;
    ::setsockopt(listen_fd.fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(::bind(listen_fd.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        result.error = "bind loopback failed: ";
        result.error += std::strerror(errno);
        return result;
    }

    socklen_t addr_len = sizeof(addr);
    if(::getsockname(listen_fd.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0)
    {
        result.error = "getsockname failed: ";
        result.error += std::strerror(errno);
        return result;
    }

    result.ok = true;
    result.port = ::ntohs(addr.sin_port);
    return result;
}

int32_t ConnectLoopback(uint16_t port)
{
    for(int32_t i = 0; i < 50; ++i)
    {
        int32_t fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if(fd < 0)
        {
            return -1;
        }

        timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);

        if(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            return fd;
        }

        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return -1;
}

bool SendAll(int32_t fd, const std::string &data)
{
    const char *cur = data.data();
    size_t left = data.size();
    while(left > 0)
    {
        ssize_t n = ::send(fd, cur, left, 0);
        if(n < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return false;
        }

        cur += n;
        left -= static_cast<size_t>(n);
    }

    return true;
}

std::string ReadAll(int32_t fd)
{
    std::string data;
    char buf[4096];
    while(true)
    {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if(n > 0)
        {
            data.append(buf, static_cast<size_t>(n));
            continue;
        }
        if(n == 0)
        {
            break;
        }
        if(errno == EINTR)
        {
            continue;
        }
        break;
    }

    return data;
}

class HttpServerTestGuard
{
public:
    HttpServerTestGuard(EventLoop *loop, std::shared_ptr<HttpServer> *server)
        :loop_(loop)
        ,server_(server)
        ,cleaned_(false)
    {}

    ~HttpServerTestGuard()
    {
        cleanup();
    }

    void cleanup()
    {
        if(cleaned_ || loop_ == nullptr)
        {
            return;
        }

        std::promise<void> done;
        auto done_future = done.get_future();
        loop_->runInLoop([this, &done](){
            if(server_)
            {
                server_->reset();
            }
            loop_->quit();
            done.set_value();
        });

        done_future.wait_for(std::chrono::seconds(2));
        cleaned_ = true;
    }

private:
    EventLoop *loop_;
    std::shared_ptr<HttpServer> *server_;
    bool cleaned_;
};

} // namespace

static const char g_test_req[] = \
"GET /index.html HTTP/1.1\r\n" \
"Host: www.chenshuo.com\r\n" \
"User-Agent: kit_muduo\r\n" \
"Content-Length: 15\r\n" \
"Content-Type: text/plain\r\n" \
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
    orireq.body().setContentType(ContentType::kPlainType);
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
    oriresp.body().setContentType(ContentType::kPlainType);
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

TEST(TestHttpServer, BusinessThreadPoolSubmitFailureReturns503)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "http_submit_failure_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<HttpServer> server;
    HttpServerTestGuard guard(loop, &server);
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&](){
        InetAddress addr(port, "127.0.0.1");
        server = std::make_shared<HttpServer>(loop, addr, "http-submit-failure-test", true, TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setBusinessThreadPoolConfig(HttpServer::BusinessThreadPoolConfig{
            1,
            0,
            2,
            10
        });
        server->Get("/slow", [](TcpConnectionPtr conn, HttpContextPtr ctx) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto resp = ctx->response();
            resp->setVersion(Version::kHttp11);
            resp->setStateCode(StateCode::k200Ok);
            resp->setConnectionClosed(true);
            resp->body().appendData("ok");
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);

    const std::string request =
        "GET /slow HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_TRUE(SendAll(client_fd.fd, request));

    const std::string response = ReadAll(client_fd.fd);
    ASSERT_NE(response.find("HTTP/1.1 503 Service Unavailable\r\n"), std::string::npos)
        << response;
    ASSERT_NE(response.find("Connection: close\r\n"), std::string::npos)
        << response;

    guard.cleanup();
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
    sd->addRoute(ExpectHttpMethods::Get, "/hello", std::make_shared<HelloServlet>("kit_proxy_server/1.0.0"));

    // 自定义函数形式
    sd->addRoute(ExpectHttpMethods::Get, "/custom", [](TcpConnectionPtr conn,  HttpContextPtr ctx) {

        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);
        resp->setStateCode(StateCode::k200Ok);
        resp->addHeader("Content-Type", "text/plain");

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
