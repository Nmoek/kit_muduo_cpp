/**
 * @file event_loop.cpp
 * @brief 事件循环对外调用类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 22:45:50
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/event_loop.h"
#include "net/poller.h"
#include "net/net_log.h"
#include "net/channel.h"

#include <sys/eventfd.h>
#include <assert.h>
#include <functional>
#include <unistd.h>

namespace kit_muduo {

thread_local EventLoop* t_loopInThread = nullptr;

/// @brief 事件循环默认超时10s
static const int32_t kPollTimeOutMs = 10000;

/**
 * @brief 创建eventfd句柄
 * @return int32_t
 */
static int32_t CreateEventFd()
{
    int32_t evfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evfd < 0)
    {
        LOOP_F_FATAL("eventfd create error! %d:%s \n", errno, strerror(errno));
        abort();
    }
    return evfd;
}

EventLoop::EventLoop()
    :_looping(false)
    ,_quit(true)
    ,_callingPendingFunc(false)
    ,_threadId(GetThreadPid())
    ,_poller(Poller::NewDefaultPoller(this))
    ,_curActiveChannel(nullptr)
    , _wakeupFd(CreateEventFd())
    ,_wakeupChannel(new Channel(this, _wakeupFd))
{

    if(t_loopInThread)
    {
        LOOP_F_FATAL("Another EventLoop exists! addr=%p, pid=%d\n",t_loopInThread, _threadId);
        abort();
    }
    else
    {
        t_loopInThread = this;
    }

    // 设置wakeup回调操作
    assert(_wakeupChannel != nullptr);
    _wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 添加wakeup读事件监听
    _wakeupChannel->enableReading();
}

EventLoop::~EventLoop()
{
    if(_wakeupChannel)
    {
        _wakeupChannel->disableAll();
        _wakeupChannel->remove();
    }
    if(_wakeupFd > 0)
        ::close(_wakeupFd);

    t_loopInThread = nullptr;
}

void EventLoop::loop()
{
    _looping = true;
    _quit = false;

    LOOP_INFO() << "EventLoop start! t=" << t_loopInThread << ", pid="  << _threadId << std::endl;

    while(!_quit)
    {
        _activeChannels.clear();
        _poolReturnTime = _poller->poll(kPollTimeOutMs, &_activeChannels);
        for(auto &c : _activeChannels)
        {
            c->handleEvent(_poolReturnTime);
        }

        // 特别注意：这里执行的是提前缓存的回调队列中的函数，而不是Channel中的读写回调函数
        doPendingFuncs();
    }

    LOOP_INFO() << "EventLoop exit! " << t_loopInThread << "pid= "  << _threadId << std::endl;

}

void EventLoop::quit()
{
    _quit = true;
    // 注意：当在线程B调用该接口，loop还睡在线程A, 需要唤醒一下
    if(!isInLoopThread())
    {
        wakeup();
    }
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    int32_t n = ::write(_wakeupFd, &one, sizeof(one));
    if(n != sizeof(one))
    {
        LOOP_F_ERROR("wakeup write error! n=%d, %d:%s \n", n, errno, strerror(errno));
    }
    return;
}

void EventLoop::runInLoop(Func cb)
{
    assert(cb != nullptr);
    if(isInLoopThread()) // 事件循环运行在当前线程则直接执行
    {
        cb();
    }
    else   //事件循环运行在其他线程则入队
    {
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(Func cb)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _pendingFuncs.emplace_back(cb);
    lock.unlock();

    // 难点：为什么要判断_callingPendingFunc
    // 答：poll会阻塞，触发一次唤醒事件，在下一轮的doPendingFuncs才能够被唤醒继续执行，否则将永远阻塞
    if(!isInLoopThread() || _callingPendingFunc)
        wakeup();
}

void EventLoop::updateChannel(Channel *channel)
{
    _poller->updateChannel(channel);
}
void EventLoop::removeChannel(Channel *channel)
{
    _poller->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel *channel)
{
    return _poller->hasChannel(channel);
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    //读取wakeup消息
    int32_t n = ::read(_wakeupFd, &one, sizeof(one));
    if(n != sizeof(one))
    {
        LOOP_F_ERROR("wakeup handleRead error! n=%d, %d:%s \n", n, errno, strerror(errno));
    }
    return;
}


void EventLoop::doPendingFuncs()
{
    std::vector<Func> tmp_func;
    _callingPendingFunc = true;

    //关键: 将待处理数据一次性交换出来, 较快地释放锁
    std::unique_lock<std::mutex> lock(_mutex);
    tmp_func.swap(_pendingFuncs);
    lock.unlock();

    for(auto &f : tmp_func)
        if(f) f();

    _callingPendingFunc = false;
}


}   // kit_muduo