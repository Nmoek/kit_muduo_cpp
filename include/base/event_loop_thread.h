/**
 * @file event_loop_thread.h
 * @brief EventLoop事件循环线程类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 20:59:53
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_EVENT_LOOP_THREAD_H__
#define __KIT_EVENT_LOOP_THREAD_H__

#include "base/noncopyable.h"
#include "base/thread.h"
#include "net/event_loop.h"

#include <mutex>
#include <condition_variable>
#include <string>

namespace kit_muduo {

class EventLoopThread: Noncopyable
{
public:
    using ThreadInitCb = std::function<void(EventLoop *)>;

    EventLoopThread(ThreadInitCb callback, const std::string &name = "");

    ~EventLoopThread();

    EventLoop* startLoop();

private:
    void threadFunc();

private:
    EventLoop *_loop;
    bool _exiting;
    Thread _thread;
    std::mutex _mutex;
    std::condition_variable _cond;
    ThreadInitCb _callback;

};

}   //kit_muduo
#endif