/**
 * @file event_loop_thread.cpp
 * @brief EventLoop事件循环线程类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 21:01:25
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/event_loop_thread.h"
#include "base/base_log.h"
#include "net/event_loop.h"
#include <cstddef>
#include <memory>

namespace kit_muduo {

EventLoopThread::EventLoopThread(EventLoopThread::ThreadInitCb callback, const std::string &name)
    :_loop(nullptr)
    ,_exiting(false)
    ,_thread(std::bind(&EventLoopThread::threadFunc, this), name)
    ,_callback(callback)
{

}

EventLoopThread::~EventLoopThread()
{
    _exiting = true;
    if(_loop)
    {
        _loop->quit();
        _thread.join();
        _loop.reset();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    _thread.start(); // 每次创建EventLoop都创建一个新线程，并且把线程中的EventLoop对象"偷"出来
    std::unique_lock<std::mutex> lock(_mutex);
    _cond.wait(lock, [this](){
        return _loop != nullptr;
    });

    return _loop.get();
}

void EventLoopThread::threadFunc()
{
    std::shared_ptr<EventLoop> loop{std::make_shared<EventLoop>()};
    if(_callback)
    {
        _callback(loop.get());
    }
    // 对外传递EventLoop
    std::unique_lock<std::mutex> lock(_mutex);
    _loop = loop;
    lock.unlock();
    _cond.notify_one();

    // 开启事件循环
    loop->loop();
    
    THREAD_INFO() << "EventLoopThread exit!" << std::endl;

}



}   //kit_muduo