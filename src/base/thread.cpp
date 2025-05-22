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

#include <semaphore.h>

namespace kit_muduo {

std::atomic_int Thread::_createdNum = 0;

Thread::Thread(ThreadFunc func, const std::string &name)
    :_started(false)
    ,_joined(false)
    ,_pid(0)
    ,_func(std::move(func))
    ,_name(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    if(_started && !_joined)
    {
        _thread->detach();
    }
}

void Thread::start()
{
    _started = true;
    sem_t sem;
    sem_init(&sem, false, 0);

    _thread = std::make_shared<std::thread>([&](){
        try
        {
            _pid = GetThreadPid();
            ::pthread_setname_np(::pthread_self(), _name.c_str());
            sem_post(&sem);
            THREAD_DEBUG() << "thread start success! " << _pid << ":" << _name << std::endl;
            _func();
        }
        catch (std::exception &e)
        {
            THREAD_ERROR() << "exception happend: " << e.what() << std::endl;
        }
    });

    // 注意: 必须确保子线程起来后才能继续往下
    sem_wait(&sem);
}

void Thread::join()
{
    if(!_thread->joinable())
    {
        THREAD_WARN() << "thread isn't joinable" << std::endl;
    }
    _joined = true;
    _thread->join();
}

void Thread::setDefaultName()
{
    int num = ++_createdNum;
    if(_name.empty())
    {
        _name += "mu_thread_";
        _name += std::to_string(num);
    }
}


}  //kit_muduo