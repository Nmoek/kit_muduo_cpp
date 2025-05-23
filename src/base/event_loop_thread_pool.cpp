/**
 * @file event_loop_thread_pool.cpp
 * @brief EventLoop事件循环线程池
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 22:54:32
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/event_loop_thread_pool.h"
#include "base/event_loop_thread.h"
#include "base/base_log.h"

namespace kit_muduo {

EventLoopThreadPool::EventLoopThreadPool(EventLoop *loop, const std::string &name)
    :_baseLoop(loop)
    ,_name(name)
    ,_started(false)
    ,_threadNums(0)
    ,_next(-1)
{

}

void EventLoopThreadPool::start(const ThreadInitCb &callback)
{
    _started = true;
    EventLoop *loop = nullptr;
     // 多Reactor模型
    for(int i = 0;i < _threadNums;++i)
    {
        std::string tmp = _name;
        tmp += "_";
        tmp += std::to_string(i);
        std::unique_ptr<EventLoopThread> t(new EventLoopThread(callback, tmp));

        loop = t->startLoop();

        _threads.emplace_back(std::move(t));
        _loops.emplace_back(loop);

    }

    // 单Reactor模型
    if(0 == _threadNums && callback)
    {
        callback(_baseLoop);
    }

}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    _next = _loops.empty() ? 0 : (_next + 1) % _threadNums;
    return _loops.empty() ? _baseLoop : _loops[_next];
}


}   //kit_muduo