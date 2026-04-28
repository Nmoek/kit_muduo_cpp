/**
 * @file thread_pool.cpp
 * @brief 线程池 14/17版本
 * @author Kewin Li
 * @version 2.0
 * @date 2025-04-20 11:27:11
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/thread_pool.h"
#include "base/base_log.h"

#include <future>
#include <memory>
#include <thread>
#include <iostream>
#include <mutex>
#include <unistd.h>

namespace kit_muduo {

ThreadPool::ThreadPool(int32_t initThreadCount)
    :initThreadCount_(initThreadCount)
    ,curThreadCount_(0)
    ,busyCount_(0)
    ,threadMaxThreshHold_(THREAD_MAX_THRESHHOLD)
    ,isRun_(false)
    ,threadMaxIdleInterval_(THREAD_MAX_IDLE_INTERVAL)
    ,curTaskCount_(0)
    ,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    ,mode_(FIXED_MOD)
{
}


ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::start()
{
    if(initThreadCount_ <= 0)
    {
        throw std::invalid_argument("thread count is invalid!!");
    }
    
    isRun_ = true;
    for(int i = 0;i < initThreadCount_;++i)
    {
        auto th = std::make_unique<WorkThread>();
        th->setPoolFunc(std::bind(&ThreadPool::threadRunFunc, this, std::placeholders::_1));

        ++curThreadCount_;
        pool_.emplace(th->getGenerateId(), std::move(th));
    }

    //启动公平性 初始化完之后一起启动
    for(auto &t : pool_)
        (t.second)->start();

    usleep(50);

    TPOOL_INFO() << "thread pool start success!" << std::endl;
}

void ThreadPool::stop()
{
    isRun_ = false;
    notEmpty_.notify_all();
    notFull_.notify_all();
    // 退出条件
    // 任务队列空
    // 忙碌线程=0

    {
    TPOOL_INFO() << "thread pool start exit...." << std::endl;
    std::unique_lock<std::mutex> lock(taskQueMutex_);
    waitExit_.wait(lock, [this](){
        TPOOL_DEBUG() << "waitExit_ notify"
            << ", task size= " << curTaskCount_
            << ", " << busyCount_ << "/" << curThreadCount_
            << std::endl;
        return checkExit();
    });
    }

    TPOOL_INFO() << "thread pool exit success" << std::endl;

}

void ThreadPool::setMode(PoolMode mode)
{
    if(checkState())
        return;

    mode_ = mode;
}

void ThreadPool::threadRunFunc(uint32_t generateId)
{
    auto waitSt = std::cv_status::no_timeout;

    TPOOL_INFO()<< "worker " << generateId << " thread start!!" << std::endl;
    while(1)
    {
        std::unique_lock<std::mutex> lock(taskQueMutex_);
        if(CACHE_MOD == mode_)
        {
            while(curTaskCount_ <= 0
                && isRun_)
            {
                // 空闲超过 30s的线程需要退出
                waitSt = notEmpty_.wait_for(lock, std::chrono::seconds(THREAD_MAX_IDLE_INTERVAL));
                if(std::cv_status::timeout == waitSt)
                {

                    if(isClear())
                    {
                        TPOOL_INFO() << "generateId= " << generateId << " ,notEmpty_ wait timeout, not get task!" << std::endl;

                        delThread(generateId);
                        return;
                    }
                    else  // 不需要清理的线程进入下一次循坏
                    {
                        continue;
                    }
                }
            }
        }
        else
        {
            notEmpty_.wait(lock, [this](){
                return taskQue_.size() > 0 || !isRun_;
            });
        }
        // 注意: 这里两种回收策略
        // 1. 不拿任务 直接退出
        // 2. 先拿任务 然后 再退出
        if(!isRun_ && taskQue_.size() <= 0)
            break;

        TPOOL_INFO() << generateId << " run task= " << taskQue_.size() << ", isRun= " << isRun_ << std::endl;

        auto task = taskQue_.front();
        taskQue_.pop();
        --curTaskCount_;

        lock.unlock();

        notFull_.notify_one();

        if(task)
        {
            ++busyCount_;
            task();
            --busyCount_;
        }
    }
    TPOOL_DEBUG() << "worker " << generateId << " thread exit!!" << std::endl;
    waitExit_.notify_one();
}

void ThreadPool::setTaskQueMaxThreshHold(int32_t threshhold)
{
    if(checkState())
        return;
    taskQueMaxThreshHold_ = threshhold;
}

int32_t ThreadPool::getTaskQueMaxThreshHold() const
{
    return taskQueMaxThreshHold_;
}


void ThreadPool::setThreadMaxThreshHold(int32_t threshhold)
{
    if(checkState())
        return;
    threadMaxThreshHold_ = std::max(threshhold, initThreadCount_);
}


int32_t ThreadPool::getThreadMaxThreshHold() const
{
    return threadMaxThreshHold_;
}

void ThreadPool::setThreadMaxIdleInterval(int32_t interval_s)
{
    if(checkState())
        return;
    threadMaxIdleInterval_ = interval_s;
}

int32_t ThreadPool::getThreadMaxIdleInterval() const
{
    return threadMaxIdleInterval_;
}



void ThreadPool::addThread()
{
    if(!checkState()) // 如果池不运行 则不扩容
        return;

    auto th = std::make_unique<WorkThread>();
    th->setPoolFunc(std::bind(&ThreadPool::threadRunFunc, this, std::placeholders::_1));

    TPOOL_INFO() << "id= " << th->getGenerateId() << " thread create, " << curThreadCount_ << "/" << threadMaxThreshHold_ << std::endl;

    th->start();
    ++curThreadCount_;
    pool_.emplace(th->getGenerateId(), std::move(th));
}

void ThreadPool::delThread(uint32_t id)
{
    if(!checkState()) // 如果池不运行 则不清理
        return;

    auto it = pool_.find(id);
    if(it != pool_.end())
    {
        --curThreadCount_;
        TPOOL_INFO() << it->second->getGenerateId() << " clear! " << curThreadCount_ << "/" << threadMaxThreshHold_  << std::endl;
        pool_.erase(it);
    }
}



/*************WorkThread**************/
uint32_t WorkThread::s_generateId = 0;

WorkThread::WorkThread()
    :Thread(std::bind(&WorkThread::workFunc, this), std::to_string(s_generateId) + "_worker")
    ,_id(s_generateId++)
    ,_poolFunc(nullptr)
{

}


uint32_t WorkThread::getGenerateId() const
{
    return _id;
}

void WorkThread::workFunc()
{
    if(_poolFunc)
        _poolFunc(_id);
    else
        TPOOL_F_WARN("work thread %d dont exist valid workFunc, exit!\n", _id);
}

} // namespace kit