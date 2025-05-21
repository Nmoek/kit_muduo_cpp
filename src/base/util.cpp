/**
 * @file util.cpp
 * @brief 常用工具
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-18 23:27:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/util.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <sys/time.h>
#include <string>

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
} // namespace kit