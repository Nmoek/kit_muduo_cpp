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
#include <string>

namespace kit_muduo {

class Thread
{
public:
    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc func, const std::string &name = "");
    ~Thread();

    void start();
    void join();

    bool started() const { return started_; }
    bool joined() const { return joined_; }
    static int32_t createdNum() { return _createdNum; }

    std::string name() const { return name_; }

    pid_t pid() const { return pid_; }

private:
    void setDefaultName();

private:
    static std::atomic_int _createdNum;

private:
    bool started_;
    bool joined_;
    std::shared_ptr<std::thread> thread_;
    pid_t pid_;
    ThreadFunc func_;
    std::string name_;

};





}   //kit_muduo


#endif