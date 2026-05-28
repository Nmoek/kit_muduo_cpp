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
#include <atomic>
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

bool StopTcpServer(EventLoop *loop, const std::shared_ptr<TcpServer> &server)
{
    auto stopped = std::make_shared<std::promise<void>>();
    auto stopped_future = stopped->get_future();

    loop->runInLoop([server, stopped]() {
        if(server)
        {
            server->stopAsync([stopped]() {
                stopped->set_value();
            });
        }
        else
        {
            stopped->set_value();
        }
    });

    return stopped_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
}

bool ResetTcpServerAndQuitLoop(EventLoop *loop, std::shared_ptr<TcpServer> *server)
{
    auto done = std::make_shared<std::promise<void>>();
    auto done_future = done->get_future();

    loop->runInLoop([loop, server, done]() {
        if(server)
        {
            server->reset();
        }
        loop->quit();
        done->set_value();
    });

    return done_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
}

} // namespace

/*
测试思路：
1. 构造并 start 一个 TcpServer，但故意不调用 stop/stopAsync。
2. 让对象离开作用域触发析构，验证 debug 构建下的强断言能倒逼调用方先停止服务。

示例：
  EventLoop
      |
      v
  TcpServer::start()
      |
      v
  ~TcpServer() without stop  ==>  assert
*/
#ifndef NDEBUG
// 这个用例暂时不测试 析构时保底stop()暂时调用
TEST(TestTcpServer, DISABLED_DestructorAssertsWhenStartedServerWasNotStopped)
{
    ASSERT_DEATH({
        EventLoop loop;
        TcpServer server(
            &loop,
            InetAddress(0, "127.0.0.1"),
            "tcp-server-must-stop",
            TcpServer::KReusePort);
        server.setThreadNum(0);
        server.start();
    }, "_started");
}
#endif

/*
测试思路：
1. 服务端连接建立后，在业务线程中跨线程调用 TcpConnection::send(std::string)。
2. send 入队后立刻污染业务线程栈上字符串，验证发送任务持有了自己的 payload 副本。
3. 用 stopAsync 等待服务停止完成，再释放 TcpServer，满足析构契约。

示例：
  client <--- "tcp cross thread string payload"
              ^
              |
       business thread send(local_payload)
              |
              v
       local_payload overwritten
*/
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

    ASSERT_TRUE(StopTcpServer(loop, server));
    ASSERT_TRUE(ResetTcpServerAndQuitLoop(loop, &server));
}

/*
测试思路：
1. 启动一个最小 echo server，客户端发送一段文本并期望原样返回。
2. 服务端发送回包后 shutdown，覆盖正常读写和关闭路径。
3. 用 stopAsync 完成最终清理，避免析构时还处于 started 状态。

示例：
  client -- "hello tcp\n" --> TcpServer
  client <-- "hello tcp\n" -- TcpServer
*/
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

    ASSERT_TRUE(StopTcpServer(loop, server));
    ASSERT_TRUE(ResetTcpServerAndQuitLoop(loop, &server));
}

/*
测试思路：
1. 启动 server 并建立一个活跃连接，记录连接名。
2. 调用 stopAsync，等待 done 回调，断言连接表已经清空且连接断开回调已经触发。
3. 客户端侧 recv 应读到 EOF，说明 TcpConnection 已经释放底层 socket。

示例：
  client ---- connect ----> TcpServer connections[name]
                              |
                              v
                         stopAsync(done)
                              |
                              v
  client <------- EOF ---- connectDestroyed + socket close
*/
TEST(TestTcpServer, StopAsyncClosesActiveConnectionAndRunsDoneAfterCleanup)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "tcp_stop_async_active_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<TcpServer> server;
    std::promise<void> started;
    auto started_future = started.get_future();
    std::promise<std::string> connected;
    auto connected_future = connected.get_future();
    std::atomic_bool connected_once{false};
    std::atomic_int disconnected_count{0};

    loop->runInLoop([&]() {
        server = std::make_shared<TcpServer>(
            loop,
            InetAddress(port, "127.0.0.1"),
            "tcp-stop-async-active-test",
            TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setConnectionCallback([&](const TcpConnectionPtr &conn) {
            if(conn->connected())
            {
                if(!connected_once.exchange(true))
                {
                    connected.set_value(conn->name());
                }
                return;
            }
            disconnected_count.fetch_add(1);
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_EQ(connected_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const std::string conn_name = connected_future.get();
    ASSERT_NE(server->getConnection(conn_name), nullptr);

    ASSERT_TRUE(StopTcpServer(loop, server));

    EXPECT_EQ(server->getConnection(conn_name), nullptr);
    EXPECT_EQ(disconnected_count.load(), 1);

    char ch = 0;
    const ssize_t n = ::recv(client_fd.fd, &ch, sizeof(ch), 0);
    EXPECT_EQ(n, 0) << "recv result=" << n << ", errno=" << errno << ":" << std::strerror(errno);

    ASSERT_TRUE(ResetTcpServerAndQuitLoop(loop, &server));
}

/*
测试思路：
1. 启动后第一次 stopAsync 应完成 acceptor 停止并触发 done。
2. 第二次 stopAsync 是幂等调用，也必须触发调用方传入的 done，避免上层等待卡死。

示例：
  stopAsync(done1)  ==> done1
  stopAsync(done2)  ==> done2
*/
TEST(TestTcpServer, StopAsyncInvokesCallbackWhenAlreadyStopped)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "tcp_stop_async_twice_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<TcpServer> server;
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&]() {
        server = std::make_shared<TcpServer>(
            loop,
            InetAddress(port, "127.0.0.1"),
            "tcp-stop-async-twice-test",
            TcpServer::KReusePort);
        server->setThreadNum(0);
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    ASSERT_TRUE(StopTcpServer(loop, server));

    auto stopped_again = std::make_shared<std::promise<void>>();
    auto stopped_again_future = stopped_again->get_future();
    loop->runInLoop([server, stopped_again]() {
        server->stopAsync([stopped_again]() {
            stopped_again->set_value();
        });
    });
    ASSERT_EQ(stopped_again_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    ASSERT_TRUE(ResetTcpServerAndQuitLoop(loop, &server));
}

/*
测试思路：
1. 从非 loop 线程调用同步 stop()，它应等待 acceptor 和连接清理完成后再返回。
2. stop() 返回后立刻把析构任务投递到 loop 线程，析构断言不能触发，也不能留下捕获 TcpServer this 的延迟任务。

示例：
  test thread: server->stop()
                    |
                    v
               wait stopAsync done
                    |
                    v
  loop thread: server.reset()
*/
TEST(TestTcpServer, StopWaitsForAsyncCleanupBeforeDestructor)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "tcp_stop_sync_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<TcpServer> server;
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&]() {
        server = std::make_shared<TcpServer>(
            loop,
            InetAddress(port, "127.0.0.1"),
            "tcp-stop-sync-test",
            TcpServer::KReusePort);
        server->setThreadNum(0);
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    server->stop();

    ASSERT_TRUE(ResetTcpServerAndQuitLoop(loop, &server));
}

/*
测试思路：
1. 使用 1 个 sub loop，让连接关闭发生在 sub loop，removeConnectionInLoop 排队到 base loop。
2. 人为阻塞 base loop，使“客户端关闭排队的 remove”和“主线程 stopAsync 抢先 swap 连接表”同时存在。
3. 释放 base loop 后，旧的 removeConnectionInLoop 应识别为 stale conn 并跳过，不能二次 connectDestroyed。

示意：
  client close
      |
      v
  sub loop: handleClose()
      |
      v
  base loop: [blocked] -> queued removeConnectionInLoop(conn)

  main thread: stopAsync()
      |
      v
  swap connections + sub loop connectDestroyed(conn)
      |
      v
  release base loop -> stale remove should skip
*/
TEST(TestTcpServer, StopAsyncSkipsQueuedRemoveConnectionAfterPeerClose)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "tcp_stop_async_stale_remove_base");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<TcpServer> server;
    std::promise<void> started;
    auto started_future = started.get_future();
    std::promise<void> connected;
    auto connected_future = connected.get_future();
    std::atomic_bool connected_once{false};

    loop->runInLoop([&]() {
        server = std::make_shared<TcpServer>(
            loop,
            InetAddress(port, "127.0.0.1"),
            "tcp-stop-async-stale-remove-test",
            TcpServer::KReusePort);
        server->setThreadNum(1);
        server->setConnectionCallback([&](const TcpConnectionPtr &conn) {
            if(conn->connected() && !connected_once.exchange(true))
            {
                connected.set_value();
            }
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_EQ(connected_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto blocker_entered = std::make_shared<std::promise<void>>();
    auto blocker_entered_future = blocker_entered->get_future();
    auto release_blocker = std::make_shared<std::promise<void>>();
    std::shared_future<void> release_future = release_blocker->get_future().share();

    loop->queueInLoop([blocker_entered, release_future]() mutable {
        blocker_entered->set_value();
        release_future.wait();
    });
    ASSERT_EQ(blocker_entered_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    ::close(client_fd.fd);
    client_fd.fd = -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto stopped = std::make_shared<std::promise<void>>();
    auto stopped_future = stopped->get_future();
    server->stopAsync([stopped]() {
        stopped->set_value();
    });

    release_blocker->set_value();
    ASSERT_EQ(stopped_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    ASSERT_TRUE(ResetTcpServerAndQuitLoop(loop, &server));
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
