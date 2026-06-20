/**
 * @file test_http_server.cpp
 * @brief HTTP server 行为测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "base/event_loop_thread.h"
#include "net/event_loop.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_server.h"
#include "net/http/http_util.h"
#include "net/inet_address.h"
#include "net/tcp_server.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <future>
#include <memory>
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

        auto stopped = std::make_shared<std::promise<void>>();
        auto stopped_future = stopped->get_future();
        loop_->runInLoop([this, stopped](){
            if(server_ && *server_)
            {
                (*server_)->stopAsync([stopped](){
                    stopped->set_value();
                });
            }
            else
            {
                stopped->set_value();
            }
        });
        stopped_future.wait_for(std::chrono::seconds(2));

        auto done = std::make_shared<std::promise<void>>();
        auto done_future = done->get_future();
        loop_->runInLoop([this, done](){
            if(server_)
            {
                server_->reset();
            }
            loop_->quit();
            done->set_value();
        });

        done_future.wait_for(std::chrono::seconds(2));
        cleaned_ = true;
    }

private:
    EventLoop *loop_;
    std::shared_ptr<HttpServer> *server_;
    bool cleaned_;
};


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



} // namespace

/*
测试思路：
1. 启动本地 HttpServer，并在一个 TCP 连接中连续发送两条 HTTP request。
2. server 应分别派发 /one 和 /two，不能把第二条请求吞掉或合并进第一条。
3. 第二条请求带 Connection: close，响应后连接应正常关闭。

示例：
  GET /one\r\n\r\nGET /two\r\nConnection: close\r\n\r\n
        |
        v
  response body 依次包含 /one 和 /two
*/
TEST(TestHttpServer, pipelined_requests_are_dispatched_separately)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "http_pipeline_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<HttpServer> server;
    HttpServerTestGuard guard(loop, &server);
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&](){
        InetAddress addr(port, "127.0.0.1");
        server = std::make_shared<HttpServer>(loop, addr, "http-pipeline-test", false, TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setHttpCallback([](TcpConnectionPtr conn, HttpContextPtr ctx) {
            auto req = ctx->request();
            auto resp = ctx->response();
            resp->setVersion(Version::kHttp11);
            resp->setStateCode(StateCode::k200Ok);
            resp->body().appendData(req->path());
            if(req->path() == "/two")
            {
                resp->setConnectionClosed(true);
            }
            conn->send(resp->toString());
            if(resp->connectionClosed())
            {
                conn->shutdown();
            }
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);

    const std::string requests =
        "GET /one HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "\r\n"
        "GET /two HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_TRUE(SendAll(client_fd.fd, requests));

    const std::string response = ReadAll(client_fd.fd);
    const size_t first_resp = response.find("HTTP/1.1 200 OK\r\n");
    ASSERT_NE(first_resp, std::string::npos) << response;

    const size_t first_body = response.find("\r\n\r\n/one", first_resp);
    ASSERT_NE(first_body, std::string::npos) << response;

    const size_t second_resp = response.find("HTTP/1.1 200 OK\r\n", first_resp + 1);
    ASSERT_NE(second_resp, std::string::npos) << response;

    const size_t second_body = response.find("\r\n\r\n/two", second_resp);
    ASSERT_NE(second_body, std::string::npos) << response;

    guard.cleanup();
}

/*
测试思路：
1. 启用 HTTP business thread pool，并把队列容量设置得很小。
2. handler 主动 sleep，使提交任务路径进入队列满/提交失败分支。
3. server 应返回 503，而不是挂住连接或静默断开。

示例：
  GET /slow + business queue unavailable
        |
        v
  HTTP/1.1 503 Service Unavailable, Connection: close
*/
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

/*
测试思路：
1. 手动启动固定地址上的 HTTP server，便于人工联调监听行为。
2. 该用例会长期进入 loop.loop()，默认禁用。
3. 需要人工运行时再去掉 DISABLED_ 前缀。

示例：
  listen 192.168.77.136:5555
        |
        v
  外部客户端手动访问
*/
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


/*
测试思路：
1. 手动启动带 ServletDispatch 的 HTTP server。
2. 注册固定 servlet 和 lambda servlet，便于人工验证路由分发。
3. 该用例会长期运行，默认禁用。

示例：
  GET /hello 或 GET /custom
        |
        v
  servlet dispatch 返回对应响应
*/
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
