/**
 * @file test_buffer.cpp
 * @brief 读写缓存测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 02:55:52
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/buffer.h"
#include "./test_log.h"

#include "gtest/gtest.h"

#include <cerrno>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

using namespace kit_muduo;

namespace {

struct FdGuard
{
    explicit FdGuard(int32_t fd = -1)
        :fd(fd)
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

bool WriteAll(int32_t fd, const std::string &data)
{
    const char *cur = data.data();
    size_t left = data.size();
    while(left > 0)
    {
        ssize_t n = ::write(fd, cur, left);
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

std::string MakePattern(size_t len)
{
    std::string data;
    data.reserve(len);
    for(size_t i = 0; i < len; ++i)
    {
        data.push_back(static_cast<char>('a' + (i % 26)));
    }
    return data;
}

void MakeSocketPair(FdGuard &left, FdGuard &right)
{
    int32_t fds[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0) << ::strerror(errno);
    left.fd = fds[0];
    right.fd = fds[1];
}

} // namespace


TEST(TestBuffer, test)
{
    Buffer b;
    char data[] = {"sdfwerfwefewfew5f156few165f1ewf"};
    b.append(data, sizeof(data));

    std::string s = b.resetAllAsString();

    EXPECT_EQ(::memcmp(data, s.data(), sizeof(data)), 0);
    TEST_INFO() << "read data: " << s << std::endl;
}

TEST(TestBuffer, ResetAsDataClampsToReadableBytes)
{
    Buffer b;
    const std::string data = "abc";
    b.append(data.data(), data.size());

    auto res = b.resetAsData(data.size() + 16);

    ASSERT_EQ(res.size(), data.size());
    ASSERT_EQ(std::string(res.begin(), res.end()), data);
    ASSERT_EQ(b.readableBytes(), 0u);
    ASSERT_EQ(b.prependBytes(), 8u);
}

TEST(TestBuffer, ReadFdExactWritableBytes)
{
    Buffer b;
    const size_t writable = b.writableBytes();
    const std::string payload = MakePattern(writable);

    FdGuard read_fd;
    FdGuard write_fd;
    MakeSocketPair(read_fd, write_fd);
    ASSERT_TRUE(WriteAll(write_fd.fd, payload));

    int32_t saved_errno = 0;
    ssize_t n = b.readFd(read_fd.fd, &saved_errno);

    ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    ASSERT_EQ(saved_errno, 0);
    ASSERT_EQ(b.readableBytes(), payload.size());
    ASSERT_EQ(b.writableBytes(), 0u);
    ASSERT_EQ(b.lookAsString(payload.size()), payload);
}

TEST(TestBuffer, ReadFdAppendsOverflowBytes)
{
    Buffer b;
    const size_t writable = b.writableBytes();
    const std::string payload = MakePattern(writable + 1);

    FdGuard read_fd;
    FdGuard write_fd;
    MakeSocketPair(read_fd, write_fd);
    ASSERT_TRUE(WriteAll(write_fd.fd, payload));

    int32_t saved_errno = 0;
    ssize_t n = b.readFd(read_fd.fd, &saved_errno);

    ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    ASSERT_EQ(saved_errno, 0);
    ASSERT_EQ(b.readableBytes(), payload.size());
    ASSERT_EQ(b.lookAsString(payload.size()), payload);
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
