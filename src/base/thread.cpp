/**
 * @file thread.cpp
 * @brief 线程类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 12:53:49
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/thread.h"
#include "base/util.h"
#include "base/base_log.h"

#include <cerrno>
#include <semaphore.h>
#include <unistd.h>

namespace kit_muduo {

std::atomic_int Thread::_createdNum(0);

Thread::Thread(ThreadFunc func, const std::string &name)
    :started_(false)
    ,joined_(false)
    ,pid_(0)
    ,func_(func)
    ,name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    if(started_ && !joined_)
    {
        thread_->detach();
    }
}

void Thread::start()
{
    started_ = true;
    sem_t sem;
    if(sem_init(&sem, 0, 0) != 0)
    {
        THREAD_F_ERROR("sem_init failed: %d:%s\n", errno, strerror(errno));
        abort();
    }

    thread_ = std::make_shared<std::thread>([this, 
        name = name_, 
        func = func_,
        &sem](){
        try
        {
            this->pid_ = GetThreadPid();

            if(!name.empty() && strlen(name.c_str()) > 0)
            {
                ::pthread_setname_np(::pthread_self(), name.c_str());
            }

            sem_post(&sem);
            if(func)
            {
                func();
            }
        }
        catch (std::exception &e)
        {
            THREAD_F_ERROR("!!![EXCEPTION]!!! func run error: %s \n", e.what());
        }
    });

    // 注意: 必须确保子线程起来后才能继续往下
    while(-1 == sem_wait(&sem))
    {
        if(EINTR == errno)
        {
            continue;
        }
        THREAD_F_ERROR("sem_wait failed: %d:%s\n", errno, strerror(errno));
        usleep(100);
        break;
    }
    sem_destroy(&sem);
}

void Thread::join()
{
    if(!thread_ || !thread_->joinable())
    {
        THREAD_WARN() << "thread isn't joinable" << std::endl;
        return;
    }
    thread_->join();
    joined_ = true;

}

void Thread::setDefaultName()
{
    int num = ++_createdNum;
    if(name_.empty())
    {
        name_ += "mu_thread_";
        name_ += std::to_string(num);
    }
}


}  //kit_muduo