/**
 * @file bounded_lock_free_queue.h
 * @brief  MPMC(多生多消费) 无锁队列
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-09 02:46:13
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __BOUNDED_LOCK_FREE_QUEUE_H__
#define __BOUNDED_LOCK_FREE_QUEUE_H__ 

#include "base/noncopyable.h"

#include <memory>
#include <atomic>
#include <stdexcept>
#include <type_traits>

namespace kit_muduo {

template<typename T>
class BoundedLockFreeQueue: Noncopyable
{
public:
    explicit BoundedLockFreeQueue(size_t capacity)
        :capacity_(normalizeCapacity(capacity))
        ,mask_(capacity_ - 1)
        ,buffer_(std::make_unique<Cell[]>(capacity_))
    {
        static_assert(std::is_nothrow_move_constructible<T>::value,
            "BoundedLockFreeQueue works best with nothrow move constructible T");
        static_assert(
        std::atomic<size_t>::is_always_lock_free,
            "BoundedLockFreeQueue expects lock-free std::atomic_size_t");

        for(size_t i = 0;i < capacity_;++i)
        {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }

        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief 特别注意: 无锁队列必须等所有生产消费者都停止才能进入到数据销毁环节
     */
    ~BoundedLockFreeQueue() noexcept
    {
        size_t enqueu_pos = enqueue_pos_.load(std::memory_order_relaxed);
        size_t dequeue_pos = dequeue_pos_.load(std::memory_order_relaxed);
        
        while(dequeue_pos < enqueu_pos)
        {
            Cell &cell = buffer_[dequeue_pos & mask_];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            if(dequeue_pos + 1 == seq)
            {
                cell.ptr()->~T();
            }
            ++dequeue_pos;
        }
    }

    // 注意: 这里使用值参数为了方便move语义，如果push失败原对象也默认丢弃
    bool tryPush(T value) noexcept
    {
        Cell *cell = nullptr;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for(;;)
        {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            // 槽位允许操作 可写
            if(0 == diff)
            {
                // BMW操作
                if(enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    break;
                }

            }
            // 槽位已被其他生产者写入 重新读取新的入队下标
            else if(diff > 0)
            {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
            // 槽位还未被读取 队列满
            else // if(diff < 0)
            {
                return false;
            }
        }

        new (cell->ptr()) T(std::move(value)); // 显式构造
        // 保证元素构造不会被重排到 sequence 发布之后
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(T &out) noexcept
    {
        Cell *cell = nullptr;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        for(;;)
        {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if(0 == diff)
            {
                if(dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    break;
                }
            }
            // 槽位已被其他消费者读取 重新读取新的出队下标
            else if(diff > 0)
            {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
            // 没有数据可读 队列空
            else // if(diff < 0)
            {
                return false;
            }
        }
        out = std::move(*cell->ptr());
        cell->ptr()->~T(); // 显示析构
        //保证 move 和析构不会被重排到“槽位可写”发布之后
        cell->sequence.store(pos + capacity_, std::memory_order_release); 
        return true;
    }

    size_t capacity() const noexcept{ return capacity_; }
    size_t size() const noexcept
    { 
        auto enqueue_pos = enqueue_pos_.load(std::memory_order_relaxed);
        auto dequeue_pos = dequeue_pos_.load(std::memory_order_relaxed);
        return enqueue_pos >= dequeue_pos ? enqueue_pos - dequeue_pos : 0;
    }

    bool empty() const noexcept { return size() == 0; }

private:
    struct Cell
    {
        /// @brief 表示该槽位当前期待哪个全局位置来操作它
        std::atomic<size_t> sequence;
        /// @brief 根据类型先分配空间但不触发构造函数
        typename std::aligned_storage<sizeof(T), alignof(T)>::type data;

        T* ptr()
        {
            return reinterpret_cast<T*>(&data);
        }

        const T* ptr() const
        {
            return reinterpret_cast<const T*>(&data);
        }
    };

    /**
     * @brief 将队列容易归一化为2的整数次幂(目的: 取模优化为位运算)
     * @param capacity 
     * @return size_t 
     */
    static size_t normalizeCapacity(size_t capacity)
    {
        if(capacity <= 1)
        {
            throw std::invalid_argument("queue capacity disallow <= 1");
        }
        size_t normalized = 1;
        while(normalized < capacity)
        {
            normalized <<= 1;
        }
        return normalized;
    }

private:
    /// @brief 队列容量
    size_t capacity_{0};
    /// @brief 槽位下标取模用 永远=capacity_ - 1
    size_t mask_{0};
    /// @brief ring buffer槽位
    std::unique_ptr<Cell[]> buffer_;

    /// @brief 注意以下两行是是CPU缓存优化 单行一次64字节
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};
};



}
#endif // __BOUNDED_LOCK_FREE_QUEUE_H__