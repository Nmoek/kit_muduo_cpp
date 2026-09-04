/**
 * @file thread_group.h
 * @brief 线程组
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-31 20:17:10
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_THREAD_GROUP_H__
#define __KIT_THREAD_GROUP_H__

#include "base/noncopyable.h"
#include "base/thread.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <future>
#include <utility>
#include <vector>

namespace kit_muduo {

/**
 * @brief 并发线程组(非线程安全)
 */
class ThreadGroup: Noncopyable 
{
public:
    /// @brief 停止谓语 用于在任务开始前优先停止当前线程
    using StopPredicate = std::function<bool()>;
    using Task = std::function<void(const StopPredicate&)>;

    class Package
    {
    private:
        std::string name;
        Task task;

        Package(std::string name, Task task)
            : name(std::move(name)),
            task(std::move(task)) {}

        template <typename>
        friend struct PreparedPackage;
        friend class ThreadGroup;
    };

    template <typename RType>
    struct PreparedPackage 
    {
        Package package;
        std::future<RType> future;
    };

    template <typename FuncType>
    static auto MakePackage(std::string name, FuncType&& func) ->PreparedPackage<std::invoke_result_t<std::decay_t<FuncType>, const StopPredicate&>>
    {
        using DecayFuncType = std::decay_t<FuncType>;

        using RType = std::invoke_result_t<std::decay_t<FuncType>, const StopPredicate&>;

        using PackagedTaskType = std::packaged_task<RType(const StopPredicate&)>;

        auto task = std::make_shared<PackagedTaskType>(
            std::forward<FuncType>(func)
        );
        auto future = task->get_future();

        Package package{
            std::move(name),
            [task](const StopPredicate& stop) mutable 
            {
                (*task)(stop);
            }
        };

        return PreparedPackage<RType>{
            std::move(package),
            std::move(future)
        };
    }

    explicit ThreadGroup(std::vector<Package> packages);

    ~ThreadGroup();

    /**
     * @brief 开启所有线程组
     * @return std::shared_ptr<ErrorGroup> 
     */
     
    void startAll();

    /**
     * @brief 等待所有线程执行完毕
     */
    void wait();

    /**
     * @brief 未开始前取消(如果线程已经开始执行 则无法取消)
     */
    void requestStop() noexcept;

    bool joined() const noexcept;
    bool stopping() const noexcept;

private:
    enum class State 
    {
        kCreated,
        kRunning,
        kJoining,
        kJoined
    };

    struct Entry
    { 
        std::string name;
        std::unique_ptr<Thread> thread{nullptr};
    };

    ThreadGroup::Entry makeEntry(Package package);
    void joinAll();
    void joinAllNoExcept() noexcept;


    std::vector<Entry> entries_;
    std::atomic<State> state_{State::kCreated};
    std::shared_ptr<std::atomic<bool>> stopping_;
};




}
#endif // __KIT_THREAD_GROUP_H__