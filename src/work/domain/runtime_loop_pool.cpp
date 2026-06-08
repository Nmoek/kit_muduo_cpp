/**
 * @file runtime_loop_pool.cpp
 * @brief 运行态事件循环池
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-26 01:17:36
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/domain_log.h"
#include "domain/runtime_loop_pool.h"
#include "base/event_loop_thread.h"
#include "domain/runtime_result.h"

#include <exception>
#include <memory>
#include <stdexcept>

namespace kit_domain {


RuntimeLease::RuntimeLease(RuntimeLoopPool* pool,
    kit_muduo::EventLoop* loop,
    size_t slot_index,
    ProjectRuntimeUid uid,
    uint64_t token)
    :pool_(pool)
    ,loop_(loop)
    ,slot_index_(slot_index)
    ,uid_(std::move(uid))
    ,token_(token)
    ,released_(false)
{

}

RuntimeLease::~RuntimeLease()
{
    release();
}

void RuntimeLease::release() noexcept
{
    bool expected = false;
    if(!released_.compare_exchange_strong(expected, true))
    {
        return;
    }

    if(pool_)
    {
        pool_->release(slot_index_, token_);
    }

}




RuntimeLoopPool::RuntimeLoopPool(size_t capacity, const std::string& prefix_name)
    :slots_(capacity > 0 ? std::make_unique<Slot[]>(capacity) : nullptr)
    ,capacity_(capacity)
    ,is_shutdown_(false)
    ,active_count_(0)
{
    if(!slots_)
    {
        throw std::invalid_argument("runtime loop slots malloc error");
    }
    for(int i = 0;i < capacity_;++i)
    {
        slots_[i].loop_thread_ptr = std::make_unique<kit_muduo::EventLoopThread>(nullptr, std::to_string(i) + prefix_name);

        slots_[i].loop = slots_[i].loop_thread_ptr->startLoop();
        if(!slots_[i].loop)
        {
            is_shutdown_ = true;
            throw std::runtime_error("runtime loop start error!");
        }
    }

}

RuntimeLoopPool::~RuntimeLoopPool()
{
    shutdown();
}


RuntimeResult<std::shared_ptr<RuntimeLease>> RuntimeLoopPool::acquire(ProjectRuntimeUid uid) noexcept
{
    RuntimeResult<std::shared_ptr<RuntimeLease>> result;
    if(0 == capacity_ || is_shutdown_.load())
    {
        result.error.set(RuntimeError::kRuntimeLoopPoolStopped);
        return result;
    }


    for(int i = 0;i < capacity_;++i)
    {
        // 偶数空闲 奇数忙碌
        Slot &slot = slots_[i];
        uint64_t token = slot.token.load();
        while((token & 1U) == 0)
        {
            uint64_t busy = token + 1;
            /*核心无锁技法: 
                1. 如果 slot.token 当前仍然等于 token，
                就把 slot.token 改成 busy，并返回 true。

                2. 如果 slot.token 当前已经不是 token，
                说明别人抢先改了它，CAS 失败，返回 false，
                同时把 token 更新成当前真实值。
            */
            if(slot.token.compare_exchange_weak(token, busy))
            {
                try 
                {
                    result.val.reset(new RuntimeLease(this, slot.loop, i, std::move(uid), busy));
                    ++active_count_;
                }
                catch(const std::exception &e)
                {
                    // 回滚忙碌状态
                    uint64_t expected = busy;
                    slot.token.compare_exchange_strong(expected, busy + 1);
                    // 回归内存
                    result.val.reset();
                    RUNTIME_F_ERROR("runtime loop lease create  error!\n");
                    result.error.set(RuntimeError::kRuntimeLoopPoolInvalidLease);
                }

                return result;
            }
        }
    }
    
    result.error.set(RuntimeError::kRuntimeLoopPoolExhausted);
    return result;
}

void RuntimeLoopPool::shutdown() noexcept
{
    bool expected = false;
    if(!is_shutdown_.compare_exchange_strong(expected, true) )
    {
        return;
    }

    if(0 != active_count_.load())
    {
        RUNTIME_F_ERROR("runtime loop still active: %ld\n", active_count_.load());
        is_shutdown_.store(false);
        return;
    }

    for(int i = 0;i < capacity_;++i)
    {
        if(slots_[i].loop)
        {
            slots_[i].loop->quit();
        }
    }
}


void RuntimeLoopPool::release(size_t slot_index, uint64_t token) noexcept
{
    if(slot_index >= capacity_ || 0 == token)
    {
        RUNTIME_F_ERROR("slot_index/token invliad! %ld %ld\n", slot_index, token);
        return;
    }

    Slot &slot = slots_[slot_index];
    uint64_t expected = token;
    if(slot.token.compare_exchange_strong(expected, token + 1))
    {
        --active_count_;
    }

}

}
