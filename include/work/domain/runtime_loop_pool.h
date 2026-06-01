/**
 * @file runtime_loop_pool.h
 * @brief 运行态事件循环池
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-25 20:36:33
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_RUNIME_LOOP_POOL_H__
#define __KIT_RUNIME_LOOP_POOL_H__

#include "base/noncopyable.h"
#include "domain/runtime_result.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <variant>
#include <vector>

namespace kit_muduo {
class EventLoop;
class EventLoopThread;
};

namespace kit_domain {

using ProjectRuntimeUid = std::variant<int64_t, std::string>;


class RuntimeLoopPool;

/**
 * @brief 运行态Loop租约
 */
class RuntimeLease: kit_muduo::Noncopyable
{
public:
    friend class RuntimeLoopPool;

    ~RuntimeLease();

    kit_muduo::EventLoop* loop() const noexcept { return loop_; }
    size_t slotIndex() const noexcept { return slot_index_; }

    const ProjectRuntimeUid& uid() const noexcept { return uid_; }
    uint64_t token() const noexcept { return token_; }

    bool isRelease() const { return released_.load(); }

    void release() noexcept;

private:

    RuntimeLease(RuntimeLoopPool* pool,
        kit_muduo::EventLoop* loop,
        size_t slot_index,
        ProjectRuntimeUid uid,
        uint64_t token);

private:
    RuntimeLoopPool* pool_{nullptr};
    kit_muduo::EventLoop* loop_{nullptr};
    size_t slot_index_{0};
    ProjectRuntimeUid uid_;
    uint64_t token_{0};
    std::atomic_bool released_{false};
};


class RuntimeLoopPool: kit_muduo::Noncopyable
{
public:
    friend class RuntimeLease;


    RuntimeLoopPool(size_t capacity = kDefaultSlotCount, const std::string& prefix_name = "Rloop");

    ~RuntimeLoopPool();

    RuntimeResult<std::shared_ptr<RuntimeLease>> acquire(ProjectRuntimeUid uid) noexcept;

    void shutdown() noexcept;

    size_t capacity() const noexcept { return capacity_; }
    size_t activeCount() const noexcept { return active_count_.load(); }

private:
    void release(size_t slot_index, uint64_t token) noexcept;

private:

    /// @brief 目前最大容纳100测试用例同时使用(后续配置)
    static constexpr int32_t kDefaultSlotCount = 100;

    struct Slot
    {
        std::unique_ptr<kit_muduo::EventLoopThread> loop_thread_ptr;
        kit_muduo::EventLoop *loop{nullptr};
        std::atomic_uint64_t token{0};
    };

    std::unique_ptr<Slot[]> slots_;
    size_t capacity_{0};
    std::atomic_bool is_shutdown_{false};
    std::atomic_size_t active_count_{0};
};


}
#endif //__KIT_RUNIME_LOOP_POOL_H__