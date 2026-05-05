/**
 * @file thread_pool.h
 * @brief 线程池 14/17版本
 * @author Kewin Li
 * @version 2.0
 * @date 2025-04-20 10:42:42
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __THREAD_POOL_14_17_H__
#define __THREAD_POOL_14_17_H__

#include "base/thread.h"

#include <vector>
#include <queue>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <functional>
#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <thread>
#include <unordered_map>
#include <future>

namespace kit_muduo {

static const int32_t THREAD_MAX_THRESHHOLD = 100;
static const int32_t TASK_MAX_THRESHHOLD = INT32_MAX;
static const int32_t THREAD_MAX_IDLE_INTERVAL = 30;  //单位s


class ThreadPool;

class WorkThread: public Thread
{
public:
    using UPtr = std::unique_ptr<WorkThread>;
    using PoolFunc = std::function<void(uint32_t)>;

    WorkThread(ThreadPool *raw_pool);

    ~WorkThread() = default;

    void setPoolFunc(PoolFunc func) { pool_func_ = std::move(func); }

    /**
     * @brief 获取线程唯一ID
     * @return uint32_t
     */
    uint32_t getGenerateId() const;


private:
    void workFunc();

private:
    /// @brief 线程唯一ID
    static uint32_t s_generateId;

private:
    uint32_t id_;
    PoolFunc pool_func_;
    ThreadPool* raw_pool_;
};

/**
 * @brief 线程池类
 */
class ThreadPool
{

public:
    enum PoolMode {
        FIXED_MOD = 1,  //固定数量模式
        CACHE_MOD    ,  //动态扩容模式
    };

    enum SubmitStatus {
        kOK,
        kStopping,
        kTimeout,
    };

    template<class T>
    struct SubmitResult
    {
        SubmitStatus status{kOK};
        std::future<T> result_future{};

        bool ok() const { return status == SubmitStatus::kOK; }
    };

public:

    ThreadPool(int32_t initThreadCount = std::thread::hardware_concurrency());

    ~ThreadPool();

    void start();

    void stop();
    /**
     * @brief 设置线程池启动模式
     * @param[in] mode 模式
     */
    void setMode(PoolMode mode);

    /**
     * @brief 设置任务队列数量上限
     * @param[in] threshhold
     */
    void setTaskQueMaxThreshHold(int32_t threshhold);

    /**
     * @brief 获取任务队列数量上限
     * @return int32_t
     */
    int32_t getTaskQueMaxThreshHold() const;


    /**
     * @brief 设置线程数量上限
     * @param[in] threshhold
     */
     void setThreadMaxThreshHold(int32_t threshhold);

     /**
      * @brief 获取线程数量上限
      * @return int32_t
      */
    int32_t getThreadMaxThreshHold() const;

    /**
     * @brief 设置线程最大空闲间隔
     * @param interval_s
     */
    void setThreadMaxIdleInterval(int32_t interval_s);

    /**
     * @brief 获取线程最大空闲间隔
     * @return int32_t
     */
    int32_t getThreadMaxIdleInterval() const;

    /**
     * @brief 带超时提交任务并返回提交状态
     * @tparam FuncType 任务函数类型
     * @tparam Args 任务函数可变参数
     * @param interval_ms 超时时间
     * @param func
     * @param args
     * @return SubmitResult<decltype(func(args...))> 
     */
    template<typename FuncType, typename... Args>
    auto trySubmitTask(int32_t interval_ms, FuncType &&func, Args&&... args) -> SubmitResult<decltype(func(args...))>
    {
        using ReturnType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<FuncType>(func), std::forward<Args>(args)...)
        );
        auto result = SubmitResult<ReturnType>();
        result.status = SubmitStatus::kOK;

        if(!is_running_)
        {
            result.status = SubmitStatus::kStopping;
            return result;
        }


        std::unique_lock<std::mutex> lock(task_que_mutex_);

        auto waitCondFunc = [this](){

            return cur_task_count_ < task_que_max_threshhold_ || !is_running_;
        };

        if(-1 == interval_ms)
        {
            notFull_.wait(lock, waitCondFunc);
        }
        else
        {
            if(!notFull_.wait_for(lock, std::chrono::milliseconds(interval_ms), waitCondFunc))
            {
                // 注意 先解队列锁
                lock.unlock();

                // 扩容
                if(CACHE_MOD == mode_)
                {
                    addThread();
                }

                result.status = SubmitStatus::kTimeout;
                return result;
            }
        }

        if(!is_running_)
        {
            result.status = SubmitStatus::kStopping;
            return result;
        }

        // taskQue_.push(ptask);
        task_que_.emplace([task](){ (*task)(); });
        ++cur_task_count_;
        lock.unlock();
        notEmpty_.notify_one();

        // 扩容
        addThread();
        
        
        result.result_future = std::move(task->get_future());
        return result;
    }


    /**
     * @brief 阻塞提交任务
     * @tparam FuncType 任务函数类型
     * @tparam Args 任务函数可变参数
     * @param func
     * @param args
     * @return std::future<decltype(func(args...))>
     */
    template<class FuncType, class ...Args>
    auto submitTask(FuncType &&func, Args&& ...args) -> SubmitResult<decltype(func(args...))>
    {
        return trySubmitTask(-1, std::forward<FuncType>(func), std::forward<Args>(args)...);
    }

    /**
     * @brief 暴露给WorkThread单独使用的接口
     * @param generateId 
     */
    void markThreadExitedWithException(uint32_t generateId);

private:
    /**
     * @brief 线程入口函数, 处理业务
     */
    void threadRunFunc(uint32_t generateId);


    inline bool checkState() const
    {
        return is_running_;
    }

    inline bool isCache() const
    {
        //只要 任务数 >  空闲线程数就扩容
        return CACHE_MOD == mode_
        && cur_thread_count_ < thread_max_threshhold_
        &&  cur_task_count_ > (cur_thread_count_ - busy_count_);
    }


    inline bool isClear() const
    {
        return CACHE_MOD == mode_
        && cur_thread_count_ > init_thread_count_
        &&  cur_task_count_ < (cur_thread_count_ - busy_count_);
    }

    inline bool checkExit() const
    {
        // 1. 只要全是空闲线程就退出
        // return busyCount_ == 0;
        // 2. 全是空闲线程 && 任务执行完 才退出
        return busy_count_ == 0
            && cur_task_count_ == 0;
    }

    /**
     * @brief 增加线程
     */
    void addThread();


    /**
     * @brief 标记退出线程
     * @param generateId 
     * @return true 
     * @return false 
     */
    bool markThreadExited(uint32_t generateId);


    /**
     * @brief 机会式清理work thread对象空间
     */
    std::vector<WorkThread::UPtr> cleanupExitedThreadsUnLock();

    /**
     * @brief 池退出时回收线程资源和清理对象空间
     */
    void joinAndClearThreads();

private:

    /// @brief 线程集合
    std::unordered_map<uint32_t, WorkThread::UPtr> pool_;
    /// @brief 已退出线程的id集合
    std::vector<uint32_t> exited_ids_;
    /// @brief 线程集合锁
    std::mutex pool_mutex_;
    /// @brief 线程初始数量
    int32_t init_thread_count_{0};
    /// @brief 当前已创建的线程数量
    std::atomic_int32_t cur_thread_count_{0};
    /// @brief 正在忙碌线程数量
    std::atomic_int32_t busy_count_{0};
    /// @brief 线程数量上限
    int32_t thread_max_threshhold_;
    /// @brief 线程池运行状态
    std::atomic_bool is_running_{false};
    /// @brief 线程最大空闲时间
    int32_t thread_max_idle_interval_{0};


    using Task = std::function<void()>;
    /// @brief 任务队列
    std::queue<Task> task_que_;
    /// @brief 当前未处理任务数量
    std::atomic_int32_t cur_task_count_{0};
    /// @brief 任务队列数量上限
    int32_t task_que_max_threshhold_;
    /// @brief 任务队列锁
    std::mutex task_que_mutex_;
    /// @brief 任务队列不空条件变量
    std::condition_variable notEmpty_;
    /// @brief 任务队列不满条件变量
    std::condition_variable notFull_;
    /// @brief 线程池退出条件变量
    std::condition_variable waitExit_;

    /// @brief 当前线程池模式
    PoolMode mode_{FIXED_MOD};
};


} // namespace kit
#endif