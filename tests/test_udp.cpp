/**
 * @file test_udp.cpp
 * @brief UDP相关接口测试
 * @author Kewin Li
 * @version 2.0
 * @date 2026-03-30
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "./test_log.h"
#include "gtest/gtest.h"
#include "net/inet_address.h"
#include "net/event_loop.h"
#include "net/socket.h"
#include "net/udp_datagram.h"
#include "base/thread.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <future>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace kit_muduo;

namespace {

/**
 * @brief UDP测试夹具
 *
 * 说明：
 * 1. `TEST_F` 会基于这个夹具类创建测试环境。
 * 2. 当多个测试都需要同一批辅助函数时，使用夹具最清晰。
 * 3. 后续你给 RTP、RTSP 写测试时，也可以延续这种写法。
 */
class UdpDatagramTest : public ::testing::Test
{
protected:
    /**
     * @brief 生成本地回环地址
     * @param[in] port 端口，传 0 表示让系统分配临时端口
     * @return InetAddress
     */
    static InetAddress makeLoopbackAddress(uint16_t port)
    {
        return InetAddress(port, "127.0.0.1");
    }

    /**
     * @brief 给 socket 设置接收超时，避免测试异常时无限阻塞
     * @param[in] fd socket 文件描述符
     * @param[in] timeout_ms 超时时间，单位毫秒
     */
    static void setRecvTimeoutMs(int32_t fd, int32_t timeout_ms)
    {
        struct timeval timeout = {};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ASSERT_EQ(ret, 0) << "setsockopt(SO_RCVTIMEO) failed";
    }

    /**
     * @brief 把字节数组转成字符串，便于断言
     * @param[in] message UDP报文数据
     * @return std::string
     */
    static std::string toString(const std::vector<uint8_t> &message)
    {
        return std::string(message.begin(), message.end());
    }

    /**
     * @brief 探测当前环境是否允许创建UDP socket
     *
     * 说明：
     * 1. 当前工程里的 `Socket::CreateUdpIpv4()` 在失败时会直接 `abort()`。
     * 2. 某些受限环境下，测试进程会因为这个行为被直接打断。
     * 3. 先用原生 `::socket()` 做一次探测，失败时直接 `GTEST_SKIP()` 更稳妥。
     */
    static bool isUdpSocketAvailable(std::string *error_message)
    {
        const int32_t probe_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (probe_fd < 0)
        {
            if (error_message != nullptr)
            {
                *error_message = "UDP socket unavailable in current environment, errno=" +
                    std::to_string(errno) + ", error=" + std::strerror(errno);
            }
            return false;
        }
        ::close(probe_fd);
        return true;
    }
};

}  // namespace

/**
 * @brief 同步发送 + 同步接收 模板
 *
 * 这个用例主要演示：
 * 1. 如何绑定随机端口，避免多个测试抢固定端口。
 * 2. 如何写最简单的“客户端发包，服务端回包”验证。
 * 3. `ASSERT_*` 和 `EXPECT_*` 的基本使用场景。
 *
 * 理解重点：
 * - `ASSERT_*` 失败后当前测试立即结束。
 *   适合“后续步骤依赖当前结果”的场景。
 * - `EXPECT_*` 失败后测试继续执行。
 *   适合“只是多记录一个失败信息”的场景。
 */
TEST_F(UdpDatagramTest, SyncSendToAndRecvFrom)
{
    std::string skip_reason;
    if (!isUdpSocketAvailable(&skip_reason))
    {
        GTEST_SKIP() << skip_reason;
    }

    const std::string request = "hello udp";
    const std::string response = "hello client";

    const int32_t server_fd = Socket::CreateUdpIpv4(false);
    UdpDatagram server(nullptr, "udp_sync_server", server_fd);
    server.bind(makeLoopbackAddress(0));
    setRecvTimeoutMs(server.fd(), 1000);
    const InetAddress server_addr = InetAddress::GetLocalAddr(server.fd());

    const int32_t client_fd = Socket::CreateUdpIpv4(false);
    UdpDatagram client(nullptr, "udp_sync_client", client_fd);
    client.bind(makeLoopbackAddress(0));
    setRecvTimeoutMs(client.fd(), 1000);
    const InetAddress client_addr = InetAddress::GetLocalAddr(client.fd());

    const ssize_t client_send_size = client.sendTo(request.data(), request.size(), server_addr);
    ASSERT_EQ(client_send_size, static_cast<ssize_t>(request.size()));

    std::vector<uint8_t> server_recv_message;
    InetAddress client_peer_addr;
    const ssize_t server_recv_size = server.recvFrom(server_recv_message, client_peer_addr);
    ASSERT_GT(server_recv_size, 0) << "server recvFrom timeout or failed";
    EXPECT_EQ(toString(server_recv_message), request);
    EXPECT_EQ(client_peer_addr.toPort(), client_addr.toPort());

    const ssize_t server_send_size = server.sendTo(response.data(), response.size(), client_peer_addr);
    ASSERT_EQ(server_send_size, static_cast<ssize_t>(response.size()));

    std::vector<uint8_t> client_recv_message;
    InetAddress server_peer_addr;
    const ssize_t client_recv_size = client.recvFrom(client_recv_message, server_peer_addr);

    ASSERT_GT(client_recv_size, 0) << "client recvFrom timeout or failed";
    EXPECT_EQ(toString(client_recv_message), response);
    EXPECT_EQ(server_peer_addr.toPort(), server_addr.toPort());
}

/**
 * @brief 异步接收回调模板
 *
 * 这个用例主要演示：
 * 1. `UdpDatagram` 的“异步”本质是挂到 `EventLoop` 的事件回调，而不是 `EventLoopThread`。
 * 2. 测试线程自己持有 `EventLoop`，对端发包放到普通线程里完成。
 * 3. 如何用 `std::promise/std::future` 等待回调结果，再由回调主动退出事件循环。
 *
 * 这种写法更贴近单 Reactor 场景，也更符合 `UdpDatagram` 当前的封装语义。
 */
TEST_F(UdpDatagramTest, AsyncReceiveCallback)
{
    std::string skip_reason;
    if (!isUdpSocketAvailable(&skip_reason))
    {
        GTEST_SKIP() << skip_reason;
    }

    const std::string request = "udp async message";

    struct AsyncRecvResult
    {
        std::string message;
        InetAddress peer_addr;
        TimeStamp receive_time;
    };

    EventLoop loop;
    auto server = std::make_shared<UdpDatagram>(&loop, "udp_async_server", Socket::CreateUdpIpv4());
    server->bind(makeLoopbackAddress(0));
    const InetAddress server_addr = InetAddress::GetLocalAddr(server->fd());

    std::promise<AsyncRecvResult> recv_promise;
    std::future<AsyncRecvResult> recv_future = recv_promise.get_future();

    server->setMessageCallback([&](const std::vector<uint8_t> &message,
                                   const InetAddress &peer_addr,
                                   TimeStamp receive_time) {
        AsyncRecvResult result;
        result.message = toString(message);
        result.peer_addr = peer_addr;
        result.receive_time = receive_time;
        recv_promise.set_value(result);
        loop.quit();
    });

    std::promise<InetAddress> client_addr_promise;
    std::future<InetAddress> client_addr_future = client_addr_promise.get_future();
    Thread client_thread([&]() {
        UdpDatagram client(nullptr, "udp_async_client", Socket::CreateUdpIpv4(false));
        client.bind(makeLoopbackAddress(0));
        client_addr_promise.set_value(InetAddress::GetLocalAddr(client.fd()));
        (void)client.sendTo(request.data(), request.size(), server_addr);
    });
    client_thread.start();

    loop.runAfter(2000, [&loop]() {
        loop.quit();
    });
    loop.loop();

    ASSERT_EQ(client_addr_future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    ASSERT_EQ(recv_future.wait_for(std::chrono::seconds(0)), std::future_status::ready)
        << "async receive callback timeout";

    const InetAddress client_addr = client_addr_future.get();
    const AsyncRecvResult recv_result = recv_future.get();

    EXPECT_EQ(recv_result.message, request);
    EXPECT_EQ(recv_result.peer_addr.toPort(), client_addr.toPort());
    EXPECT_GT(recv_result.receive_time.millSeconds(), 0U);
}

/**
 * @brief 异步发送模板
 *
 * 这个用例主要演示：
 * 1. 非 `EventLoop` 所在线程调用 `send()` 时，会通过 `queueInLoop()` 回到 IO 线程。
 * 2. `write_complete_callback_` 是异步发送完成通知，不需要依赖 `EventLoopThread`。
 * 3. 接收端依旧能按“一条报文一次接收”拿到完整消息。
 *
 * 这个模板后续可以直接扩展为：
 * - RTP 连续发送测试
 * - 多个报文连续发送测试
 * - 错误回调测试
 */
TEST_F(UdpDatagramTest, AsyncSendCallback)
{
    std::string skip_reason;
    if (!isUdpSocketAvailable(&skip_reason))
    {
        GTEST_SKIP() << skip_reason;
    }

    const std::string message = "udp async send";

    struct AsyncSendRecvResult
    {
        ssize_t recv_size = -1;
        std::string message;
        InetAddress peer_addr;
    };

    EventLoop loop;
    auto sender = std::make_shared<UdpDatagram>(&loop, "udp_async_sender", Socket::CreateUdpIpv4());
    sender->bind(makeLoopbackAddress(0));
    const InetAddress sender_addr = InetAddress::GetLocalAddr(sender->fd());

    std::promise<void> write_done_promise;
    std::future<void> write_done_future = write_done_promise.get_future();
    sender->setWriteCompleteCallback([&]() {
        write_done_promise.set_value();
        loop.quit();
    });

    std::promise<InetAddress> receiver_addr_promise;
    std::future<InetAddress> receiver_addr_future = receiver_addr_promise.get_future();
    std::promise<AsyncSendRecvResult> recv_result_promise;
    std::future<AsyncSendRecvResult> recv_result_future = recv_result_promise.get_future();

    Thread receiver_thread([&]() {
        UdpDatagram receiver(nullptr, "udp_async_receiver", Socket::CreateUdpIpv4(false));
        receiver.bind(makeLoopbackAddress(0));
        struct timeval timeout = {};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        (void)::setsockopt(receiver.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        receiver_addr_promise.set_value(InetAddress::GetLocalAddr(receiver.fd()));

        std::vector<uint8_t> recv_message;
        InetAddress peer_addr;
        AsyncSendRecvResult recv_result;
        recv_result.recv_size = receiver.recvFrom(recv_message, peer_addr);
        recv_result.message = toString(recv_message);
        recv_result.peer_addr = peer_addr;
        recv_result_promise.set_value(recv_result);
    });
    receiver_thread.start();

    ASSERT_EQ(receiver_addr_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const InetAddress receiver_addr = receiver_addr_future.get();

    Thread business_thread([&]() {
        sender->send(message.data(), message.size(), receiver_addr);
    });
    business_thread.start();

    loop.runAfter(2000, [&loop]() {
        loop.quit();
    });
    loop.loop();


    ASSERT_EQ(write_done_future.wait_for(std::chrono::seconds(0)), std::future_status::ready)
        << "write complete callback timeout";
    ASSERT_EQ(recv_result_future.wait_for(std::chrono::seconds(0)), std::future_status::ready)
        << "receiver recvFrom timeout or failed";

    const AsyncSendRecvResult recv_result = recv_result_future.get();
    ASSERT_GT(recv_result.recv_size, 0) << "receiver recvFrom timeout or failed";
    EXPECT_EQ(recv_result.message, message);
    EXPECT_EQ(recv_result.peer_addr.toPort(), sender_addr.toPort());
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
