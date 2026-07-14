/**
 * @file test_socket.cpp
 * @brief socket测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 00:37:44
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/socket.h"
#include "net/inet_address.h"
#include "net/acceptor.h"
#include "./test_log.h"


#include "gtest/gtest.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace kit_muduo;

namespace {

struct FdGuard
{
    explicit FdGuard(int32_t input_fd = -1)
        : fd(input_fd)
    {
    }

    ~FdGuard()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
    }

    int32_t fd;
};

bool IsLoopbackTcpSocketAvailable(std::string *error_message)
{
    FdGuard probe(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if(probe.fd < 0)
    {
        if(error_message)
        {
            *error_message = "loopback TCP socket unavailable, errno=" +
                std::to_string(errno) + ", " + std::strerror(errno);
        }
        return false;
    }
    return true;
}

} // namespace


TEST(TestSocket, DISABLED_test1)
{
    Socket s(Socket::CreateTcpIpv4());
    InetAddress addr(5555);
    s.bindAddress(addr);
    TEST_INFO() << "server listen.." << std::endl;
    s.listen();
    auto connfd = s.accept(&addr);
    TEST_INFO() << "new connect: " << connfd << ", " << addr.toIpPort() << std::endl;
}

/**
 * 测试思路：
 * 1. Acceptor 监听 127.0.0.1:0，让系统分配临时端口，避免固定 5555 端口冲突。
 * 2. 客户端主动 connect 后，EventLoop 应触发 Acceptor::handleRead 并执行 new connection 回调。
 * 3. 回调里关闭服务端 connfd 并退出 loop，测试必须自动结束，不能变成手动阻塞 server。
 *
 * 示例：
 *
 *   Acceptor(listen 127.0.0.1:0)
 *        + client connect
 *        v
 *   newConnectionCallback(connfd, peer) -> close(connfd) -> loop.quit()
 */
TEST(TestSocket, AcceptorAcceptsLoopbackConnectionAndQuits)
{
    std::string skip_reason;
    if(!IsLoopbackTcpSocketAvailable(&skip_reason))
    {
        GTEST_SKIP() << skip_reason;
    }

    EventLoop loop;
    bool accepted = false;
    FdGuard client_fd;

    try
    {
        Acceptor acceptor(&loop, InetAddress(0, "127.0.0.1"));
        acceptor.setNewConnectionCallback([&](int32_t connfd, const InetAddress &peerAddr){
            FdGuard accepted_fd(connfd);
            TEST_INFO() << "new connect: " << connfd << ", " << peerAddr.toIpPort() << std::endl;
            accepted = true;
            EXPECT_GT(connfd, 0);
            EXPECT_EQ(peerAddr.toIp(), "127.0.0.1");
            loop.quit();
        });

        acceptor.listen();
        ASSERT_TRUE(acceptor.listening());

        const InetAddress listen_addr = acceptor.getBindAddr();
        ASSERT_GT(listen_addr.toPort(), 0);
        TEST_INFO() << "server listen.. " << listen_addr.toIpPort() << std::endl;

        client_fd.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        ASSERT_GE(client_fd.fd, 0) << std::strerror(errno);
        ASSERT_EQ(::connect(client_fd.fd,
                      reinterpret_cast<const sockaddr*>(listen_addr.getSockAddr()),
                      sizeof(sockaddr_in)),
                  0) << std::strerror(errno);

        loop.runAfter(1000, [&loop](){
            loop.quit();
        });
        loop.loop();
    }
    catch(const std::exception &e)
    {
        GTEST_SKIP() << "loopback TCP acceptor unavailable: " << e.what();
    }

    EXPECT_TRUE(accepted);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
