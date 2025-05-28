/**
 * @file util.cpp
 * @brief 常用工具
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-18 23:27:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/util.h"
#include "base/base_log.h"

#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <sys/time.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

namespace kit_muduo
{

thread_local pid_t t_thread_id = 0;


uint64_t GetTimeStampMs()
{
    struct timeval tv = {0};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}


uint64_t GetCurrentUs()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

std::string Timer2Str(time_t ts, const std::string& format)
{
    struct tm tm;
    tm = *localtime(&ts);
    char buf[100];
    strftime(buf, sizeof(buf), format.c_str(), &tm);

    return buf;
}

pid_t GetThreadPid()
{
    // 编译优化  告知编译器该分支进入概率偏低
    if(__builtin_expect(t_thread_id == 0, 0))
    {
        t_thread_id = syscall(SYS_gettid);
    }
    return t_thread_id;
}

pthread_t GetThreadTid()
{
    return ::pthread_self();
}

std::string GetThreadName()
{
    char thread_name[32] = {0};
    ::pthread_getname_np(::pthread_self(), thread_name, sizeof(thread_name));
    return thread_name;
}

/**
 * @brief 创建eventfd句柄
 * @return int32_t
 */
int32_t CreateEventFd()
{
    int32_t evfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evfd < 0)
    {
        BASE_F_FATAL("util", "eventfd create error! %d:%s \n", errno, strerror(errno));
        abort();
    }
    return evfd;
}

/**
 * @brief 创建timerfd句柄
 * @return int32_t
 */
int32_t CreateTimerFd()
{
    int32_t timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(timerfd < 0)
    {
        BASE_F_FATAL("util", "timerfd_createerror! %d:%s \n", errno, strerror(errno));
        abort();
    }
    return timerfd;
}

} // namespace kit