/**
 * @file test_tcp_server.cpp
 * @brief TcpServer测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 00:12:23
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/event_loop_thread.h"
#include "base/thread.h"
#include "net/event_loop.h"
#include "net/inet_address.h"
#include "net/tcp_server.h"
#include "./test_log.h"

#include "gtest/gtest.h"

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
#include <vector>

using namespace kit_muduo;

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

std::string RecvSome(int32_t fd)
{
    std::string data;
    char buf[4096];
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if(n > 0)
    {
        data.append(buf, static_cast<size_t>(n));
    }
    return data;
}

} // namespace

TEST(TestTcpServer, CrossThreadSendStringKeepsPayloadAliveUntilLoopCallback)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "tcp_send_lifetime_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<TcpServer> server;
    std::promise<void> started;
    auto started_future = started.get_future();
    std::promise<void> connected;
    auto connected_future = connected.get_future();

    const std::string payload = "tcp cross thread string payload";
    loop->runInLoop([&]() {
        server = std::make_shared<TcpServer>(
            loop,
            InetAddress(port, "127.0.0.1"),
            "tcp-send-lifetime-test",
            TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setConnectionCallback([&](const TcpConnectionPtr &conn) {
            if(!conn->connected())
            {
                return;
            }
            connected.set_value();
            Thread business_thread([conn, payload]() {
                std::string local_payload = payload;
                conn->send(local_payload);
                local_payload.assign(4096, 'x');
                std::vector<std::vector<char>> churn;
                churn.reserve(4096);
                for(size_t i = 0; i < 4096; ++i)
                {
                    churn.emplace_back(payload.size(), 'x');
                }
            }, "tcp_send_business");
            business_thread.start();
            business_thread.join();
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_EQ(connected_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    const std::string received = RecvSome(client_fd.fd);
    EXPECT_EQ(received, payload);

    std::promise<void> done;
    auto done_future = done.get_future();
    loop->runInLoop([&]() {
        server.reset();
        loop->quit();
        done.set_value();
    });
    ASSERT_EQ(done_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

TEST(TestTcpServer, EchoServerCanExit)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "tcp_echo_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<TcpServer> server;
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&]() {
        server = std::make_shared<TcpServer>(
            loop,
            InetAddress(port, "127.0.0.1"),
            "tcp-echo-test",
            TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setConnectionCallback([](const TcpConnectionPtr &conn) {
            if(!conn->connected())
            {
                conn->shutdown();
            }
        });
        server->setMessageCallback([](const TcpConnectionPtr &conn, Buffer *buffer, TimeStamp) {
            std::string msg = buffer->resetAllAsString();
            conn->send(msg);
            conn->shutdown();
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);

    const std::string request = "hello tcp\n";
    ASSERT_TRUE(SendAll(client_fd.fd, request));
    EXPECT_EQ(RecvSome(client_fd.fd), request);

    std::promise<void> done;
    auto done_future = done.get_future();
    loop->runInLoop([&]() {
        server.reset();
        loop->quit();
        done.set_value();
    });
    ASSERT_EQ(done_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
