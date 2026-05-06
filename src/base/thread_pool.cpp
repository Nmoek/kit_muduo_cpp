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

#include <cassert>
#include <exception>
#include <future>
#include <memory>
#include <thread>
#include <iostream>
#include <mutex>
#include <unistd.h>
#include <vector>

namespace kit_muduo {

ThreadPool::ThreadPool(int32_t initThreadCount)
    :init_thread_count_(initThreadCount)
    ,cur_thread_count_(0)
    ,busy_count_(0)
    ,thread_max_threshhold_(THREAD_MAX_THRESHHOLD)
    ,is_running_(false)
    ,thread_max_idle_interval_(THREAD_MAX_IDLE_INTERVAL)
    ,cur_task_count_(0)
    ,task_que_max_threshhold_(TASK_MAX_THRESHHOLD)
    ,mode_(FIXED_MOD)
{
}


ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::start()
{
    if(checkState())
    {
        return;
    }
    if(init_thread_count_ <= 0)
    {
        throw std::invalid_argument("thread count is invalid!!");
    }
    
    is_running_ = true;
    for(int i = 0;i < init_thread_count_;++i)
    {
        auto th = std::make_unique<WorkThread>(this);
        th->setPoolFunc(std::bind(&ThreadPool::threadRunFunc, this, std::placeholders::_1));

        ++cur_thread_count_;
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
    if(!checkState())
    {
        return;
    }
    is_running_ = false;
    notEmpty_.notify_all();
    notFull_.notify_all();
    // 退出条件
    // 任务队列空
    // 忙碌线程=0

    {
    TPOOL_INFO() << "thread pool start exit...." << std::endl;
    std::unique_lock<std::mutex> lock(task_que_mutex_);
    waitExit_.wait(lock, [this](){
        TPOOL_F_DEBUG("waitExit_ notify! task size=%d, busy worker= %d/%d \n", cur_task_count_.load() ,busy_count_.load() , cur_thread_count_.load());
        return checkExit();
    });
    }

    joinAndClearThreads();
    cur_task_count_ = 0;
    cur_thread_count_ = busy_count_ = 0;

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
    bool need_exit = false;

    TPOOL_INFO()<< "worker " << generateId << " thread start!!" << std::endl;
    while(1)
    {
        std::unique_lock<std::mutex> lock(task_que_mutex_);
        if(CACHE_MOD == mode_)
        {
            while(cur_task_count_ <= 0
                && is_running_)
            {
                // 空闲超过 30s的线程需要退出
                waitSt = notEmpty_.wait_for(lock, std::chrono::seconds(thread_max_idle_interval_));
                if(std::cv_status::timeout == waitSt)
                {

                    if(checkState() && CACHE_MOD == mode_)
                    {
                        // 先解队列锁
                        lock.unlock();

                        TPOOL_DEBUG() << "notEmpty_ wait timeout, not get task!" << std::endl;

                        if(markThreadExited(generateId))
                        {
                            need_exit = true;
                            break;
                        }
                        // 重新加回 队列锁
                        lock.lock();
                        continue;
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
                return task_que_.size() > 0 || !is_running_;
            });
        }
        if(need_exit)
        {
            break;
        }
        // 注意: 这里两种回收策略
        // 1. 不拿任务 直接退出
        // 2. 先拿任务 然后 再退出(本项目采用)
        if(!is_running_ && task_que_.size() <= 0)
        {
           break;
        }

        
        TPOOL_F_INFO("wait task size=%d, isRun=%d \n", task_que_.size(), is_running_.load());

        auto task = task_que_.front();
        task_que_.pop();
        --cur_task_count_;

        lock.unlock();

        notFull_.notify_one();

        if(task)
        {
            ++busy_count_;
            try
            {
                task();
            }
            catch(const std::exception &e)
            {
                TPOOL_F_ERROR("!!![EXCEPTION]!!! work thread task run exception: %s \n", e.what());
                --busy_count_;
                markThreadExitedWithException(generateId);
                break;
            }
            --busy_count_;
        }
    }

    TPOOL_DEBUG() << "worker " << generateId << " thread exit!!" << std::endl;

    waitExit_.notify_one();
}

void ThreadPool::setTaskQueMaxThreshHold(int32_t threshhold)
{
    if(checkState())
        return;
    task_que_max_threshhold_ = threshhold;
}

int32_t ThreadPool::getTaskQueMaxThreshHold() const
{
    return task_que_max_threshhold_;
}


void ThreadPool::setThreadMaxThreshHold(int32_t threshhold)
{
    if(checkState())
        return;
    thread_max_threshhold_ = std::max(threshhold, init_thread_count_);
}


int32_t ThreadPool::getThreadMaxThreshHold() const
{
    return thread_max_threshhold_;
}

void ThreadPool::setThreadMaxIdleInterval(int32_t interval_s)
{
    if(checkState())
        return;
    thread_max_idle_interval_ = interval_s;
}

int32_t ThreadPool::getThreadMaxIdleInterval() const
{
    return thread_max_idle_interval_;
}



void ThreadPool::addThread()
{
    if(!checkState() || !isCache())
    {
        return;
    }
    std::unique_lock<std::mutex> lock(pool_mutex_);

    if(!checkState() || !isCache())
    {
        return;
    }

    // 机会式清理
    const std::vector<WorkThread::UPtr>& has_exited_threads = cleanupExitedThreadsUnLock();

    auto th = std::make_unique<WorkThread>(this);
    th->setPoolFunc(std::bind(&ThreadPool::threadRunFunc, this, std::placeholders::_1));
 
    TPOOL_INFO() << "id= " << th->getGenerateId() << " thread create, " << cur_thread_count_ << "/" << thread_max_threshhold_ << std::endl;

    ++cur_thread_count_;
    th->start();
    pool_.emplace(th->getGenerateId(), std::move(th));
    lock.unlock();

    // 先增加线程然后慢慢回收
    for(auto &t : has_exited_threads)
    {
        if(t)
        {
            TPOOL_F_INFO("work thread %d cleanuping \n", t->getGenerateId());
            t->join();
        }
    }

}


void ThreadPool::markThreadExitedWithException(uint32_t generateId)
{
    if(!checkState())
    {
        return;
    }
    std::unique_lock<std::mutex> lock(pool_mutex_);
    if(!checkState())
    {
        return;
    }
    
    exited_ids_.push_back(generateId);
    --cur_thread_count_;
    TPOOL_F_INFO("work thread exception mark! %d/%d \n", cur_thread_count_.load(),thread_max_threshhold_);
}

bool ThreadPool::markThreadExited(uint32_t generateId)
{
    if(!checkState() || !isClear())
    {
        return false;
    }
    std::unique_lock<std::mutex> lock(pool_mutex_);

    if(!checkState() || !isClear())
    {
        return false;
    }

    exited_ids_.push_back(generateId);
    --cur_thread_count_;
    TPOOL_F_INFO("work thread[%d] mark! %d/%d \n", generateId, cur_thread_count_.load(),thread_max_threshhold_);
    return true;
}

std::vector<WorkThread::UPtr> ThreadPool::cleanupExitedThreadsUnLock()
{
    std::vector<WorkThread::UPtr> has_exited_threads;

    for(auto &id : exited_ids_)
    {
        auto it = pool_.find(id);
        assert(it != pool_.end());
        has_exited_threads.push_back(std::move(it->second));
        
        auto n = pool_.erase(id);
        assert(n == 1);
    }
    exited_ids_.clear();
    return has_exited_threads;
}
    
void ThreadPool::joinAndClearThreads()
{
    std::vector<WorkThread::UPtr> joined_threads;

    std::unique_lock<std::mutex> lock(pool_mutex_);
    for(auto &it : pool_)
    {

        joined_threads.push_back(std::move(it.second));
    }
    pool_.clear();
    exited_ids_.clear();
    lock.unlock();

    for(auto &t : joined_threads)
    {
        if(t)
        {
            TPOOL_F_DEBUG("work thread %d joined! \n", t->getGenerateId());
            t->join();
        }
    }

}


/*************WorkThread**************/
uint32_t WorkThread::s_generateId = 0;

WorkThread::WorkThread(ThreadPool *raw_pool)
    :Thread(std::bind(&WorkThread::workFunc, this), std::to_string(s_generateId) + "_worker")
    ,id_(s_generateId++)
    ,pool_func_(nullptr)
    ,raw_pool_(raw_pool)
{

}


uint32_t WorkThread::getGenerateId() const
{
    return id_;
}

void WorkThread::workFunc()
{
    if(pool_func_)
    {
        try
        {
            pool_func_(id_);
        }
        catch(const std::exception &e)
        {
            TPOOL_F_ERROR("!!![EXCEPTION]!!! work thread exit exceptionly: %s \n", e.what());

            raw_pool_->markThreadExitedWithException(id_);
        }
    }
    else
    {
        TPOOL_F_WARN("work thread %d dont exist valid workFunc, exit!\n", id_);
    }
}

} // namespace kit