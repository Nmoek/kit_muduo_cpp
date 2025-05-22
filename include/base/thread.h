/**
 * @file thread.h
 * @brief 线程类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 12:47:48
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_THREAD_H__
#define __KIT_THREAD_H__

#include "base/noncopyable.h"

#include <thread>
#include <functional>
#include <memory>
#include <atomic>

namespace kit_muduo {

class Thread
{
public:
    using ThreadFunc = std::function<void()>;

    Thread(ThreadFunc func, const std::string &name = "");
    ~Thread();

    void start();
    void join();

    bool started() const { return _started; }
    bool joined() const { return _joined; }
    static int32_t createdNum() { return _createdNum; }

    std::string name() const { return _name; }

    pid_t pid() const { return _pid; }

private:
    void setDefaultName();

private:
    static std::atomic_int _createdNum;

private:
    bool _started;
    bool _joined;
    std::shared_ptr<std::thread> _thread;
    pid_t _pid;
    ThreadFunc _func;
    std::string _name;

};





}   //kit_muduo


#endif