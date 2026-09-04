/**
 * @file sem.cpp
 * @brief POSIX 信号量
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-31 11:14:48
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/sem.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <semaphore.h>
#include <stdexcept>

namespace kit_muduo {

Sem::Sem()
{
    if(::sem_init(&sem_, 0, 0) < 0)
    {
        throw std::runtime_error(std::string("semaphore init error: ") + strerror(errno));
    }
}

Sem::~Sem()
{
    ::sem_destroy(&sem_);
}

bool Sem::post()
{
    return ::sem_post(&sem_) < 0 ? false : true;
}

bool Sem::wait()
{
    return ::sem_wait(&sem_) < 0 ? false : true;
}

bool Sem::trywait()
{
    return ::sem_trywait(&sem_) < 0 ? false : true;
}

bool Sem::waitTimeout(int64_t timeout_ms)
{
    struct timespec spec = {0};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    

    for(;;)
    {
        auto now = std::chrono::steady_clock::now();
        if(now > deadline) 
        {
            return false;
        }
        else if(now == deadline)
        {
            return true;
        }

        auto monotonic_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        spec.tv_sec = monotonic_ms / 1000;
        spec.tv_nsec = (monotonic_ms % 1000) * 1000000;
        
        // 计算剩余时间
        int64_t remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        // 转换为 timespec
        spec.tv_sec += remaining / 1000;
        spec.tv_nsec += (remaining % 1000) * 1000000;

        int res = ::sem_clockwait(&sem_, CLOCK_MONOTONIC, &spec);
        if(0 == res)
        {
            break;
        }
        else if(res < 0)
        {
            if(EINTR == errno)
            {
                continue;
            }
            return false;
        }
    }
    return true;
}











}