/**
 * @file test_poll_poller.cpp
 * @brief PollPoller回归测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-05
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/channel.h"
#include "net/event_loop.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <string>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace kit_muduo;

namespace {

struct EnvGuard
{
    explicit EnvGuard(const char *name, const char *value)
        :name_(name)
    {
        const char *old_value = ::getenv(name);
        if(old_value != nullptr)
        {
            had_old_value_ = true;
            old_value_ = old_value;
        }

        ::setenv(name_.c_str(), value, 1);
    }

    ~EnvGuard()
    {
        if(had_old_value_)
        {
            ::setenv(name_.c_str(), old_value_.c_str(), 1);
        }
        else
        {
            ::unsetenv(name_.c_str());
        }
    }

    std::string name_;
    bool had_old_value_{false};
    std::string old_value_;
};

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

FdGuard MakeEventFd()
{
    int32_t fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    EXPECT_GE(fd, 0);
    return FdGuard(fd);
}

} // namespace

TEST(TestPollPoller, RemovingMiddleChannelUpdatesDisabledBackChannelIndex)
{
    EnvGuard poller_env("KIT_MUDUO_POLLER_POLL", "1");
    EventLoop loop;

    FdGuard fd1 = MakeEventFd();
    FdGuard fd2 = MakeEventFd();
    FdGuard fd3 = MakeEventFd();
    ASSERT_GE(fd1.fd, 0);
    ASSERT_GE(fd2.fd, 0);
    ASSERT_GE(fd3.fd, 0);

    Channel ch1(&loop, fd1.fd);
    Channel ch2(&loop, fd2.fd);
    Channel ch3(&loop, fd3.fd);

    ch1.enableReading();
    ch2.enableReading();
    ch3.enableReading();

    const int32_t removed_index = ch2.index();
    const int32_t disabled_old_index = ch3.index();
    ASSERT_GE(removed_index, 0);
    ASSERT_GT(disabled_old_index, removed_index);

    ch3.disableAll();
    ch2.remove();

    EXPECT_EQ(ch2.index(), -1);
    EXPECT_EQ(ch3.index(), removed_index);

    ch3.enableReading();
    EXPECT_EQ(ch3.index(), removed_index);

    ch3.remove();
    ch1.remove();
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
