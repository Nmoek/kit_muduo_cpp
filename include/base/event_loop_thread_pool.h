/**
 * @file event_loop_thread_pool.h
 * @brief EventLoop事件循环线程池
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 22:53:27
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_EVENT_LOOP_THREAD_POOL_H__
#define __KIT_EVENT_LOOP_THREAD_POOL_H__

#include "base/noncopyable.h"

#include <functional>
#include <vector>
#include <memory>

namespace kit_muduo {

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool: Noncopyable
{
public:
    using ThreadInitCb = std::function<void(EventLoop *)>;

    explicit EventLoopThreadPool(EventLoop *loop, const std::string &name = "");

    ~EventLoopThreadPool() = default;

    void setThreadNum(int32_t nums) { _threadNums = nums; }

    void start(const ThreadInitCb &callback = ThreadInitCb());

    EventLoop* getNextLoop();

    std::vector<EventLoop*> getAllLoops() {  return _loops.empty() ? std::vector<EventLoop *>(1, _baseLoop) : _loops; }

    bool started() const { return _started; }
    const std::string& name() const { return _name ;}


private:
    /// @brief Master Reactor 主事件循环
    EventLoop *_baseLoop;
    /// @brief 线程名称
    std::string _name;
    /// @brief 是否开启
    bool _started;
    /// @brief 线程创建数量
    int32_t _threadNums;
    /// @brief 取出下一个Loop的下标
    int32_t _next;
    /// @brief 每个子事件循环所在的线程池
    std::vector<std::unique_ptr<EventLoopThread>> _threads;
    /// @brief 子Reactor 子事件循环
    std::vector<EventLoop*> _loops;
};


}   //kit_muduo
#endif