/**
 * @file test_bounded_lock_free_queue.cpp
 * @brief 有界无锁队列测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-09 20:30:00
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/bounded_lock_free_queue.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace kit_muduo;

namespace {

struct MoveOnlyValue
{
    int value{-1};

    MoveOnlyValue() noexcept = default;

    explicit MoveOnlyValue(int v) noexcept
        :value(v)
    {
    }

    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;

    MoveOnlyValue(MoveOnlyValue &&other) noexcept
        :value(other.value)
    {
        other.value = -1;
    }

    MoveOnlyValue& operator=(MoveOnlyValue &&other) noexcept
    {
        if(this != &other)
        {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }
};

static_assert(std::is_nothrow_move_constructible<MoveOnlyValue>::value,
    "MoveOnlyValue must match BoundedLockFreeQueue value requirements");

struct LifetimeTrackedValue
{
    int value{0};
    static std::atomic<int> live_count;

    LifetimeTrackedValue() noexcept
    {
        ++live_count;
    }

    explicit LifetimeTrackedValue(int v) noexcept
        :value(v)
    {
        ++live_count;
    }

    LifetimeTrackedValue(const LifetimeTrackedValue&) = delete;
    LifetimeTrackedValue& operator=(const LifetimeTrackedValue&) = delete;

    LifetimeTrackedValue(LifetimeTrackedValue &&other) noexcept
        :value(other.value)
    {
        other.value = -1;
        ++live_count;
    }

    LifetimeTrackedValue& operator=(LifetimeTrackedValue &&other) noexcept
    {
        if(this != &other)
        {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }

    ~LifetimeTrackedValue() noexcept
    {
        --live_count;
    }
};

std::atomic<int> LifetimeTrackedValue::live_count{0};

bool TimedOut(const std::chrono::steady_clock::time_point &deadline)
{
    return std::chrono::steady_clock::now() >= deadline;
}

struct QueueBenchmarkResult
{
    int producer_count{0};
    int consumer_count{0};
    int items_per_producer{0};
    int total_items{0};
    size_t queue_capacity{0};
    size_t value_size{0};
    int pop_batch_size{1};
    uint64_t push_retries{0};
    uint64_t pop_misses{0};
    int consumed_items{0};
    int64_t expected_sum{0};
    int64_t actual_sum{0};
    bool queue_empty{false};
    bool timed_out{false};
    double elapsed_ms{0.0};
    double throughput_ops_per_sec{0.0};
    double ns_per_item{0.0};
};

template<typename T>
class BoundedQueueAdapter
{
public:
    explicit BoundedQueueAdapter(size_t capacity)
        :queue_(capacity)
    {
    }

    bool tryPush(T value) noexcept
    {
        return queue_.tryPush(std::move(value));
    }

    bool tryPop(T &out) noexcept
    {
        return queue_.tryPop(out);
    }

    bool empty() const noexcept
    {
        return queue_.empty();
    }

private:
    BoundedLockFreeQueue<T> queue_;
};

template<typename T>
class MutexStdQueueAdapter
{
public:
    explicit MutexStdQueueAdapter(size_t capacity)
        :capacity_(capacity)
    {
    }

    bool tryPush(T value)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if(queue_.size() >= capacity_)
        {
            return false;
        }

        queue_.push(std::move(value));
        return true;
    }

    bool tryPop(T &out)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if(queue_.empty())
        {
            return false;
        }

        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

private:
    size_t capacity_{0};
    mutable std::mutex mtx_;
    std::queue<T> queue_;
};

template<size_t PayloadBytes>
struct PayloadValue
{
    int64_t id{0};
    std::array<uint8_t, PayloadBytes> payload{};

    PayloadValue() noexcept = default;

    explicit PayloadValue(int64_t value) noexcept
        :id(value)
    {
        payload[0] = static_cast<uint8_t>(value & 0xff);
        payload[PayloadBytes - 1] = static_cast<uint8_t>((value >> 8) & 0xff);
    }

    PayloadValue(const PayloadValue&) noexcept = default;
    PayloadValue& operator=(const PayloadValue&) noexcept = default;
    PayloadValue(PayloadValue&&) noexcept = default;
    PayloadValue& operator=(PayloadValue&&) noexcept = default;
};

template<typename Queue, typename T, typename MakeValue, typename ReadId>
QueueBenchmarkResult RunQueueBenchmark(int producer_count,
                                       int consumer_count,
                                       int items_per_producer,
                                       size_t queue_capacity,
                                       int pop_batch_size,
                                       std::chrono::seconds timeout,
                                       MakeValue make_value,
                                       ReadId read_id)
{
    QueueBenchmarkResult result;
    result.producer_count = producer_count;
    result.consumer_count = consumer_count;
    result.items_per_producer = items_per_producer;
    result.total_items = producer_count * items_per_producer;
    result.queue_capacity = queue_capacity;
    result.value_size = sizeof(T);
    result.pop_batch_size = std::max(1, pop_batch_size);
    result.expected_sum =
        static_cast<int64_t>(result.total_items - 1) * static_cast<int64_t>(result.total_items) / 2;

    Queue queue(queue_capacity);
    std::atomic<bool> start{false};
    std::atomic<bool> timed_out{false};
    std::atomic<int> consumed{0};
    std::atomic<int64_t> actual_sum{0};
    std::atomic<uint64_t> push_retries{0};
    std::atomic<uint64_t> pop_misses{0};
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for(int producer = 0;producer < producer_count;++producer)
    {
        threads.emplace_back([producer,
                              items_per_producer,
                              &queue,
                              &start,
                              &timed_out,
                              &push_retries,
                              &deadline,
                              &make_value]() {
            uint64_t local_push_retries = 0;

            while(!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for(int i = 0;i < items_per_producer && !timed_out.load(std::memory_order_acquire);++i)
            {
                const int value = producer * items_per_producer + i;
                while(!queue.tryPush(make_value(value)))
                {
                    ++local_push_retries;
                    if(TimedOut(deadline))
                    {
                        push_retries.fetch_add(local_push_retries, std::memory_order_relaxed);
                        timed_out.store(true, std::memory_order_release);
                        return;
                    }
                    std::this_thread::yield();
                }
            }

            push_retries.fetch_add(local_push_retries, std::memory_order_relaxed);
        });
    }

    for(int consumer = 0;consumer < consumer_count;++consumer)
    {
        (void)consumer;
        threads.emplace_back([total_items = result.total_items,
                              pop_batch_size = result.pop_batch_size,
                              &queue,
                              &start,
                              &timed_out,
                              &consumed,
                              &actual_sum,
                              &pop_misses,
                              &deadline,
                              &read_id]() {
            uint64_t local_pop_misses = 0;
            int64_t local_sum = 0;

            while(!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            while(consumed.load(std::memory_order_acquire) < total_items &&
                  !timed_out.load(std::memory_order_acquire))
            {
                auto pop_one = [&queue, &read_id, &local_sum, &consumed]() {
                    T value{};
                    if(!queue.tryPop(value))
                    {
                        return false;
                    }

                    local_sum += read_id(value);
                    consumed.fetch_add(1, std::memory_order_release);
                    return true;
                };

                if(!pop_one())
                {
                    ++local_pop_misses;
                    if(TimedOut(deadline))
                    {
                        actual_sum.fetch_add(local_sum, std::memory_order_relaxed);
                        pop_misses.fetch_add(local_pop_misses, std::memory_order_relaxed);
                        timed_out.store(true, std::memory_order_release);
                        return;
                    }
                    std::this_thread::yield();
                    continue;
                }

                for(int i = 1;i < pop_batch_size &&
                    consumed.load(std::memory_order_acquire) < total_items;++i)
                {
                    if(!pop_one())
                    {
                        break;
                    }
                }
            }

            actual_sum.fetch_add(local_sum, std::memory_order_relaxed);
            pop_misses.fetch_add(local_pop_misses, std::memory_order_relaxed);
        });
    }

    const auto start_time = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for(auto &thread : threads)
    {
        thread.join();
    }

    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
    result.throughput_ops_per_sec =
        elapsed_seconds > 0.0 ? static_cast<double>(result.total_items) / elapsed_seconds : 0.0;
    result.ns_per_item =
        result.total_items > 0
            ? std::chrono::duration<double, std::nano>(elapsed).count() /
                  static_cast<double>(result.total_items)
            : 0.0;
    result.push_retries = push_retries.load(std::memory_order_relaxed);
    result.pop_misses = pop_misses.load(std::memory_order_relaxed);
    result.consumed_items = consumed.load(std::memory_order_relaxed);
    result.actual_sum = actual_sum.load(std::memory_order_relaxed);
    result.queue_empty = queue.empty();
    result.timed_out = timed_out.load(std::memory_order_acquire);

    return result;
}

QueueBenchmarkResult RunQueueThroughputBenchmark(int producer_count,
                                                 int consumer_count,
                                                 int items_per_producer,
                                                 size_t queue_capacity,
                                                 int pop_batch_size,
                                                 std::chrono::seconds timeout)
{
    return RunQueueBenchmark<BoundedQueueAdapter<int>, int>(
        producer_count,
        consumer_count,
        items_per_producer,
        queue_capacity,
        pop_batch_size,
        timeout,
        [](int64_t value) { return static_cast<int>(value); },
        [](int value) { return static_cast<int64_t>(value); });
}

QueueBenchmarkResult RunQueueThroughputBenchmark(int producer_count,
                                                 int consumer_count,
                                                 int items_per_producer,
                                                 size_t queue_capacity,
                                                 std::chrono::seconds timeout)
{
    return RunQueueThroughputBenchmark(
        producer_count, consumer_count, items_per_producer, queue_capacity, 1, timeout);
}

QueueBenchmarkResult RunMutexQueueThroughputBenchmark(int producer_count,
                                                      int consumer_count,
                                                      int items_per_producer,
                                                      size_t queue_capacity,
                                                      std::chrono::seconds timeout)
{
    return RunQueueBenchmark<MutexStdQueueAdapter<int>, int>(
        producer_count,
        consumer_count,
        items_per_producer,
        queue_capacity,
        1,
        timeout,
        [](int64_t value) { return static_cast<int>(value); },
        [](int value) { return static_cast<int64_t>(value); });
}

template<size_t PayloadBytes>
QueueBenchmarkResult RunPayloadThroughputBenchmark(int producer_count,
                                                   int consumer_count,
                                                   int items_per_producer,
                                                   size_t queue_capacity,
                                                   std::chrono::seconds timeout)
{
    using Value = PayloadValue<PayloadBytes>;
    static_assert(std::is_nothrow_move_constructible<Value>::value,
        "PayloadValue must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable<Value>::value,
        "PayloadValue must be nothrow move assignable");

    return RunQueueBenchmark<BoundedQueueAdapter<Value>, Value>(
        producer_count,
        consumer_count,
        items_per_producer,
        queue_capacity,
        1,
        timeout,
        [](int64_t value) { return Value(value); },
        [](const Value &value) { return value.id; });
}

void PrintQueueBenchmarkResult(const char *name, const QueueBenchmarkResult &result)
{
    std::cout << std::fixed << std::setprecision(2)
              << "\n[queue benchmark] " << name
              << "\n  producers=" << result.producer_count
              << " consumers=" << result.consumer_count
              << " capacity=" << result.queue_capacity
              << " value_size=" << result.value_size
              << " pop_batch_size=" << result.pop_batch_size
              << " total_items=" << result.total_items
              << "\n  elapsed_ms=" << result.elapsed_ms
              << " throughput_ops_per_sec=" << result.throughput_ops_per_sec
              << " ns_per_item=" << result.ns_per_item
              << "\n  push_retries=" << result.push_retries
              << " pop_misses=" << result.pop_misses
              << " consumed=" << result.consumed_items
              << " queue_empty=" << result.queue_empty
              << std::endl;
}

void AssertBenchmarkResultValid(const QueueBenchmarkResult &result)
{
    ASSERT_FALSE(result.timed_out);
    EXPECT_EQ(result.consumed_items, result.total_items);
    EXPECT_EQ(result.actual_sum, result.expected_sum);
    EXPECT_TRUE(result.queue_empty);
}

double Median(std::vector<double> values)
{
    if(values.empty())
    {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if(values.size() % 2 == 1)
    {
        return values[middle];
    }

    return (values[middle - 1] + values[middle]) / 2.0;
}

double Average(const std::vector<double> &values)
{
    if(values.empty())
    {
        return 0.0;
    }

    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

void PrintMetricSummary(const char *metric_name, const std::vector<double> &values)
{
    const auto minmax = std::minmax_element(values.begin(), values.end());
    std::cout << "  " << metric_name
              << " min=" << *minmax.first
              << " median=" << Median(values)
              << " avg=" << Average(values)
              << " max=" << *minmax.second
              << "\n";
}

void PrintQueueBenchmarkSummary(const char *name, const std::vector<QueueBenchmarkResult> &results)
{
    std::vector<double> elapsed_ms;
    std::vector<double> throughput;
    std::vector<double> ns_per_item;
    elapsed_ms.reserve(results.size());
    throughput.reserve(results.size());
    ns_per_item.reserve(results.size());

    for(const auto &result : results)
    {
        elapsed_ms.push_back(result.elapsed_ms);
        throughput.push_back(result.throughput_ops_per_sec);
        ns_per_item.push_back(result.ns_per_item);
    }

    std::cout << std::fixed << std::setprecision(2)
              << "\n[queue benchmark summary] " << name
              << "\n  runs=" << results.size()
              << " producers=" << results.front().producer_count
              << " consumers=" << results.front().consumer_count
              << " capacity=" << results.front().queue_capacity
              << " value_size=" << results.front().value_size
              << " total_items=" << results.front().total_items
              << "\n";
    PrintMetricSummary("elapsed_ms", elapsed_ms);
    PrintMetricSummary("throughput_ops_per_sec", throughput);
    PrintMetricSummary("ns_per_item", ns_per_item);
    std::cout << std::endl;
}

} // namespace

/*
 * 测试思路：
 *   1. 容量为 0 是非法输入，构造时应直接拒绝；
 *   2. 队列内部把容量归一化到 2 的整数次幂，方便 ring buffer 用 mask 取模；
 *   3. 新队列初始必须为空，size 为 0。
 *
 * 举例：
 *   用户传入容量 3，实际容量应归一化为 4；用户传入容量 5，实际容量应归一化为 8。
 *
 * 图示：
 *   input capacity: 3
 *           |
 *           v
 *   normalized ring slots: [0] [1] [2] [3]
 */
TEST(TestBoundedLockFreeQueue, RejectsZeroAndNormalizesCapacity)
{
    EXPECT_THROW({
        BoundedLockFreeQueue<int> queue(0);
        (void)queue;
    }, std::invalid_argument);

    BoundedLockFreeQueue<int> one(1);
    EXPECT_EQ(one.capacity(), 1U);
    EXPECT_TRUE(one.empty());
    EXPECT_EQ(one.size(), 0U);

    BoundedLockFreeQueue<int> three(3);
    EXPECT_EQ(three.capacity(), 4U);
    EXPECT_TRUE(three.empty());
    EXPECT_EQ(three.size(), 0U);

    BoundedLockFreeQueue<int> five(5);
    EXPECT_EQ(five.capacity(), 8U);
}

/*
 * 测试思路：
 *   1. 单生产者单消费者路径先固定最基础的 FIFO 语义；
 *   2. 容量用 2，方便明确验证满队列 tryPush 返回 false；
 *   3. 空队列 tryPop 返回 false，且不应改写调用方的 out 值。
 *
 * 举例：
 *   依次写入 10、20 后队列已满，再写入 30 应失败；随后读出顺序必须是 10、20。
 *
 * 图示：
 *   push: 10 -> 20
 *   queue: [10] [20]  full
 *   pop : 10 -> 20
 */
TEST(TestBoundedLockFreeQueue, PushPopSingleThreadKeepsFifoAndState)
{
    BoundedLockFreeQueue<int> queue(2);
    int out = -1;

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.tryPop(out));
    EXPECT_EQ(out, -1);

    ASSERT_TRUE(queue.tryPush(10));
    EXPECT_EQ(queue.size(), 1U);

    ASSERT_TRUE(queue.tryPush(20));
    EXPECT_EQ(queue.size(), 2U);
    EXPECT_FALSE(queue.empty());

    EXPECT_FALSE(queue.tryPush(30));
    EXPECT_EQ(queue.size(), 2U);

    ASSERT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out, 10);
    EXPECT_EQ(queue.size(), 1U);

    out = -1;
    ASSERT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out, 20);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0U);

    out = -1;
    EXPECT_FALSE(queue.tryPop(out));
    EXPECT_EQ(out, -1);
}

/*
 * 测试思路：
 *   1. 有界队列的槽位会循环复用，不能只验证第一轮入队/出队；
 *   2. 每轮先填满容量 2 的队列，弹出 1 个元素后再写入 1 个新元素；
 *   3. 这样可以覆盖 sequence 更新后，同一个物理槽位被再次生产者写入、消费者读出的路径。
 *
 * 举例：
 *   第 0 轮：push 0、1，pop 0，push 1000，随后应 pop 1、1000。
 *
 * 图示：
 *   round N:
 *   [a] [b] --pop a--> [_] [b] --push c--> [c] [b] --pop--> b, c
 */
TEST(TestBoundedLockFreeQueue, ReusesSlotsAfterWrapAround)
{
    BoundedLockFreeQueue<int> queue(2);
    int out = -1;

    for(int round = 0;round < 16;++round)
    {
        const int first = round * 2;
        const int second = round * 2 + 1;
        const int wrapped = 1000 + round;

        ASSERT_TRUE(queue.tryPush(first));
        ASSERT_TRUE(queue.tryPush(second));

        out = -1;
        ASSERT_TRUE(queue.tryPop(out));
        EXPECT_EQ(out, first);

        ASSERT_TRUE(queue.tryPush(wrapped));

        out = -1;
        ASSERT_TRUE(queue.tryPop(out));
        EXPECT_EQ(out, second);

        out = -1;
        ASSERT_TRUE(queue.tryPop(out));
        EXPECT_EQ(out, wrapped);

        EXPECT_TRUE(queue.empty());
        EXPECT_EQ(queue.size(), 0U);
    }
}

/*
 * 测试思路：
 *   1. 队列 tryPush 使用值参数并在槽位内 placement-new 构造，适合 move-only 数据；
 *   2. 用禁止拷贝的 MoveOnlyValue 固定不依赖拷贝构造；
 *   3. 出队时应通过移动赋值把值交给 out。
 *
 * 举例：
 *   MoveOnlyValue(7) 入队后，弹出的 out.value 应等于 7；原始对象被移动后 value 变为 -1。
 *
 * 图示：
 *   value(7) --move--> queue slot --move--> out(7)
 */
TEST(TestBoundedLockFreeQueue, SupportsMoveOnlyValues)
{
    BoundedLockFreeQueue<MoveOnlyValue> queue(2);
    MoveOnlyValue value(7);

    ASSERT_TRUE(queue.tryPush(std::move(value)));
    EXPECT_EQ(value.value, -1);

    MoveOnlyValue out;
    ASSERT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out.value, 7);
    EXPECT_TRUE(queue.empty());
}

/*
 * 测试思路：
 *   1. 队列槽位使用 aligned_storage，元素生命周期由 tryPush、tryPop 和析构函数手动管理；
 *   2. 当队列析构时，未弹出的元素必须被析构，避免泄漏对象生命周期；
 *   3. live_count 只统计当前仍活着的 LifetimeTrackedValue 实例。
 *
 * 举例：
 *   入队 3 个对象但不弹出，离开作用域后 live_count 必须回到 0。
 *
 * 图示：
 *   scope begin -> push A/B/C -> live_count = 3
 *   scope end   -> queue dtor -> live_count = 0
 */
TEST(TestBoundedLockFreeQueue, DestroysRemainingValuesOnQueueDestruction)
{
    LifetimeTrackedValue::live_count.store(0);

    {
        BoundedLockFreeQueue<LifetimeTrackedValue> queue(4);
        ASSERT_TRUE(queue.tryPush(LifetimeTrackedValue(1)));
        ASSERT_TRUE(queue.tryPush(LifetimeTrackedValue(2)));
        ASSERT_TRUE(queue.tryPush(LifetimeTrackedValue(3)));
        EXPECT_EQ(LifetimeTrackedValue::live_count.load(), 3);
    }

    EXPECT_EQ(LifetimeTrackedValue::live_count.load(), 0);
}

/*
 * 测试思路：
 *   1. 用多个 std::thread 直接压测队列的 MPMC 基本语义，不接入 Publisher；
 *   2. 每个生产者生成不重叠的整数区间，消费者记录每个整数被读到的次数；
 *   3. 所有线程结束后，每个值必须刚好出现 1 次，不能丢数据、重复消费或读出越界值。
 *
 * 举例：
 *   生产者 0 写入 [0, 999]，生产者 1 写入 [1000, 1999]；
 *   消费侧最终 seen[i] 都应等于 1。
 *
 * 图示：
 *   P0 ----\
 *   P1 -----+--> bounded queue --> C0/C1/C2/C3 --> seen[value]++
 *   P2 -----/
 *   P3 ----/
 */
TEST(TestBoundedLockFreeQueue, MultipleProducersAndConsumersTransferEachValueOnce)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 1000;
    constexpr int kTotalItems = kProducerCount * kItemsPerProducer;

    BoundedLockFreeQueue<int> queue(128);
    std::vector<std::atomic<int>> seen(kTotalItems);
    for(auto &count : seen)
    {
        count.store(0);
    }

    std::atomic<bool> start{false};
    std::atomic<bool> timed_out{false};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<int> duplicate_count{0};
    std::atomic<int> out_of_range_count{0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::vector<std::thread> threads;
    threads.reserve(kProducerCount + kConsumerCount);

    for(int producer = 0;producer < kProducerCount;++producer)
    {
        threads.emplace_back([producer, &queue, &start, &timed_out, &produced, &deadline]() {
            while(!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for(int i = 0;i < kItemsPerProducer && !timed_out.load(std::memory_order_acquire);++i)
            {
                const int value = producer * kItemsPerProducer + i;
                while(!queue.tryPush(value))
                {
                    if(TimedOut(deadline))
                    {
                        timed_out.store(true, std::memory_order_release);
                        return;
                    }
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for(int consumer = 0;consumer < kConsumerCount;++consumer)
    {
        (void)consumer;
        threads.emplace_back([&queue,
                              &seen,
                              &start,
                              &timed_out,
                              &consumed,
                              &duplicate_count,
                              &out_of_range_count,
                              &deadline]() {
            while(!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            while(consumed.load(std::memory_order_acquire) < kTotalItems &&
                  !timed_out.load(std::memory_order_acquire))
            {
                int value = -1;
                if(queue.tryPop(value))
                {
                    if(value < 0 || value >= kTotalItems)
                    {
                        out_of_range_count.fetch_add(1, std::memory_order_release);
                    }
                    else if(seen[value].fetch_add(1, std::memory_order_acq_rel) != 0)
                    {
                        duplicate_count.fetch_add(1, std::memory_order_release);
                    }
                    consumed.fetch_add(1, std::memory_order_release);
                }
                else
                {
                    if(TimedOut(deadline))
                    {
                        timed_out.store(true, std::memory_order_release);
                        return;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for(auto &thread : threads)
    {
        thread.join();
    }

    ASSERT_FALSE(timed_out.load()) << "MPMC queue transfer timed out";
    EXPECT_EQ(produced.load(), kTotalItems);
    EXPECT_EQ(consumed.load(), kTotalItems);
    EXPECT_EQ(duplicate_count.load(), 0);
    EXPECT_EQ(out_of_range_count.load(), 0);
    EXPECT_TRUE(queue.empty());

    for(int value = 0;value < kTotalItems;++value)
    {
        EXPECT_EQ(seen[value].load(), 1) << "value=" << value;
    }
}

/*
 * 测试思路：
 *   1. 基准测试只关注单生产者、单消费者的基础吞吐，不设置绝对性能阈值；
 *   2. 生产者写入连续整数，消费者读出后累加，最终用等差数列和校验没有丢数据；
 *   3. 输出 elapsed、ops/s、ns/item、push 重试和 pop 空读次数，作为本机对比基线。
 *
 * 举例：
 *   生产者写入 [0, 999999]，消费者最终 sum 应为 0 + 1 + ... + 999999。
 *
 * 图示：
 *   P0 --> queue(cap=32768) --> C0
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_SpscThroughputBaseline
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_SpscThroughputBaseline)
{
    constexpr int kProducerCount = 1;
    constexpr int kConsumerCount = 1;
    constexpr int kItemsPerProducer = 1000 * 1000;
    constexpr size_t kQueueCapacity = 32768;

    const auto result = RunQueueThroughputBenchmark(
        kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
    PrintQueueBenchmarkResult("SPSC throughput baseline", result);

    AssertBenchmarkResultValid(result);
}

/*
 * 测试思路：
 *   1. 基准测试覆盖 4 生产者、4 消费者并发竞争下的整体吞吐；
 *   2. 每个生产者写入不重叠的整数区间，消费者并发弹出并累加；
 *   3. 只断言数据完整性和无超时，不用固定耗时作为通过条件，避免机器负载造成误判。
 *
 * 举例：
 *   P0 写 [0, 249999]，P1 写 [250000, 499999]，依次类推；
 *   所有消费者合并后的 sum 应等于 0 + 1 + ... + 999999。
 *
 * 图示：
 *   P0 ----\
 *   P1 -----+--> queue(cap=65536) --> C0/C1/C2/C3
 *   P2 -----/
 *   P3 ----/
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_MpmcThroughputBaseline
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_MpmcThroughputBaseline)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 250 * 1000;
    constexpr size_t kQueueCapacity = 65536;

    const auto result = RunQueueThroughputBenchmark(
        kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
    PrintQueueBenchmarkResult("MPMC throughput baseline", result);

    AssertBenchmarkResultValid(result);
}

/*
 * 测试思路：
 *   1. 队列容量越小，生产者/消费者越容易遇到满队列或空队列，重试次数通常会增加；
 *   2. 这个用例用相同线程数和数据量，分别跑 64、1024、65536 三种容量；
 *   3. 输出每种容量的吞吐和重试次数，辅助观察容量配置对竞争行为的影响。
 *
 * 举例：
 *   cap=64 更容易出现 push_retries；cap=65536 通常重试更少，但缓存占用更大。
 *
 * 图示：
 *   4P/4C -> queue(cap=64)
 *   4P/4C -> queue(cap=1024)
 *   4P/4C -> queue(cap=65536)
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_CapacitySweep
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_CapacitySweep)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 50 * 1000;
    const std::vector<size_t> capacities = {64, 1024, 65536};

    for(size_t capacity : capacities)
    {
        const auto result = RunQueueThroughputBenchmark(
            kProducerCount, kConsumerCount, kItemsPerProducer, capacity, std::chrono::seconds(30));
        PrintQueueBenchmarkResult("capacity sweep", result);

        AssertBenchmarkResultValid(result);
    }
}

/*
 * 测试思路：
 *   1. 单次 benchmark 容易受调度和 CPU 频率波动影响，所以连续运行多轮；
 *   2. 每轮都保持同样的 4P/4C、容量和数据量，并输出每轮明细；
 *   3. 汇总 min / median / avg / max，后续对比优化前后趋势时优先看 median。
 *
 * 举例：
 *   同样的 4P/4C 跑 5 次，某一轮被系统负载干扰时，median 比单轮结果更稳。
 *
 * 图示：
 *   run1 -> run2 -> run3 -> run4 -> run5
 *                  |
 *                  v
 *       min / median / avg / max
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_MultiRunSummary
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_MultiRunSummary)
{
    constexpr int kRuns = 5;
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 100 * 1000;
    constexpr size_t kQueueCapacity = 4096;

    std::vector<QueueBenchmarkResult> results;
    results.reserve(kRuns);

    for(int run = 0;run < kRuns;++run)
    {
        auto result = RunQueueThroughputBenchmark(
            kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
        PrintQueueBenchmarkResult("multi-run lock-free", result);
        AssertBenchmarkResultValid(result);
        results.push_back(result);
    }

    PrintQueueBenchmarkSummary("multi-run lock-free", results);
}

/*
 * 测试思路：
 *   1. 只看无锁队列自身数据不够，需要一个 std::mutex + std::queue 的同语义对照组；
 *   2. 两个队列都使用 tryPush/tryPop、相同容量、相同线程数、相同数据量；
 *   3. 对比吞吐和 ns/item，观察无锁实现相对互斥锁方案在当前机器上的收益或劣势。
 *
 * 举例：
 *   4P/4C 下 lock-free 和 mutex 各搬运 40 万个 int，最终 sum 都必须正确。
 *
 * 图示：
 *   4P/4C -> BoundedLockFreeQueue
 *   4P/4C -> MutexStdQueueAdapter
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_MutexQueueComparison
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_MutexQueueComparison)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 100 * 1000;
    constexpr size_t kQueueCapacity = 4096;

    const auto lock_free = RunQueueThroughputBenchmark(
        kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
    PrintQueueBenchmarkResult("lock-free queue", lock_free);
    AssertBenchmarkResultValid(lock_free);

    const auto mutex_queue = RunMutexQueueThroughputBenchmark(
        kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
    PrintQueueBenchmarkResult("std::mutex + std::queue", mutex_queue);
    AssertBenchmarkResultValid(mutex_queue);

    if(mutex_queue.throughput_ops_per_sec > 0.0)
    {
        std::cout << std::fixed << std::setprecision(2)
                  << "\n[queue benchmark comparison] lock_free_vs_mutex throughput_ratio="
                  << lock_free.throughput_ops_per_sec / mutex_queue.throughput_ops_per_sec
                  << " ns_per_item_ratio="
                  << lock_free.ns_per_item / mutex_queue.ns_per_item
                  << std::endl;
    }
}

/*
 * 测试思路：
 *   1. 线程组合会改变竞争点：1P4C 主要观察空读，4P1C 主要观察满队列背压；
 *   2. 用相同容量和近似数据规模跑 1P1C、1P4C、4P1C、2P2C、4P4C、8P8C；
 *   3. 每个组合同时跑 BoundedLockFreeQueue 和 std::mutex + std::queue；
 *   4. 输出每种组合的吞吐、重试指标和 lock-free/mutex 吞吐比。
 *
 * 举例：
 *   4P1C 如果 push_retries 很高，说明单消费者成为瓶颈；1P4C 如果 pop_misses 很高，说明消费者过多。
 *
 * 图示：
 *   1P1C -> baseline
 *   1P4C -> consumer contention
 *   4P1C -> producer backpressure
 *   8P8C -> high contention
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_ThreadTopologyMatrix
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_ThreadTopologyMatrix)
{
    struct Topology
    {
        int producers;
        int consumers;
        int items_per_producer;
    };

    const std::vector<Topology> topologies = {
        {1, 1, 200 * 1000},
        {1, 4, 200 * 1000},
        {4, 1, 50 * 1000},
        {2, 2, 100 * 1000},
        {4, 4, 50 * 1000},
        {8, 8, 25 * 1000},
    };
    constexpr size_t kQueueCapacity = 4096;

    for(const auto &topology : topologies)
    {
        const auto lock_free = RunQueueThroughputBenchmark(topology.producers,
                                                           topology.consumers,
                                                           topology.items_per_producer,
                                                           kQueueCapacity,
                                                           std::chrono::seconds(30));
        PrintQueueBenchmarkResult("thread topology matrix lock-free", lock_free);
        AssertBenchmarkResultValid(lock_free);

        const auto mutex_queue = RunMutexQueueThroughputBenchmark(topology.producers,
                                                                  topology.consumers,
                                                                  topology.items_per_producer,
                                                                  kQueueCapacity,
                                                                  std::chrono::seconds(30));
        PrintQueueBenchmarkResult("thread topology matrix std::mutex + std::queue", mutex_queue);
        AssertBenchmarkResultValid(mutex_queue);

        if(mutex_queue.throughput_ops_per_sec > 0.0)
        {
            std::cout << std::fixed << std::setprecision(2)
                      << "\n[queue topology comparison] producers=" << topology.producers
                      << " consumers=" << topology.consumers
                      << " lock_free_vs_mutex throughput_ratio="
                      << lock_free.throughput_ops_per_sec / mutex_queue.throughput_ops_per_sec
                      << " ns_per_item_ratio="
                      << lock_free.ns_per_item / mutex_queue.ns_per_item
                      << std::endl;
        }
    }
}

/*
 * 测试思路：
 *   1. 真实业务消息不一定是 int，payload 越大，移动和缓存占用越明显；
 *   2. 使用 8B、64B、256B、1024B 四种 payload，保持 2P/2C 和相同容量；
 *   3. 每个 payload 内部带 id，消费者只累加 id，保证数据完整性校验不受 payload 内容影响。
 *
 * 举例：
 *   PayloadValue<256> 总大小约为 id + 256 字节数组，用来模拟较大的事件对象。
 *
 * 图示：
 *   small payload  -> queue -> consumers
 *   medium payload -> queue -> consumers
 *   large payload  -> queue -> consumers
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_PayloadSizeMatrix
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_PayloadSizeMatrix)
{
    constexpr int kProducerCount = 2;
    constexpr int kConsumerCount = 2;
    constexpr int kItemsPerProducer = 50 * 1000;
    constexpr size_t kQueueCapacity = 4096;

    {
        const auto result = RunPayloadThroughputBenchmark<8>(
            kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
        PrintQueueBenchmarkResult("payload 8B", result);
        AssertBenchmarkResultValid(result);
    }
    {
        const auto result = RunPayloadThroughputBenchmark<64>(
            kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
        PrintQueueBenchmarkResult("payload 64B", result);
        AssertBenchmarkResultValid(result);
    }
    {
        const auto result = RunPayloadThroughputBenchmark<256>(
            kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
        PrintQueueBenchmarkResult("payload 256B", result);
        AssertBenchmarkResultValid(result);
    }
    {
        const auto result = RunPayloadThroughputBenchmark<1024>(
            kProducerCount, kConsumerCount, kItemsPerProducer, kQueueCapacity, std::chrono::seconds(30));
        PrintQueueBenchmarkResult("payload 1024B", result);
        AssertBenchmarkResultValid(result);
    }
}

/*
 * 测试思路：
 *   1. 业务侧如果一次被唤醒后连续 drain 多个元素，可以摊薄调度和空读开销；
 *   2. 这里不修改队列接口，只让消费者在成功 pop 后最多连续多 pop 8 个元素；
 *   3. 对比 batch=1 和 batch=8 的吞吐、pop_misses，判断批量消费是否值得在业务层使用。
 *
 * 举例：
 *   C0 成功 pop 一个元素后，继续尝试最多 7 次 pop；遇到空队列就停止本轮 drain。
 *
 * 图示：
 *   producer -> queue -> consumer pop x 1
 *   producer -> queue -> consumer pop x up to 8
 *
 * 运行方式：
 *   ./bin/test_bounded_lock_free_queue --gtest_also_run_disabled_tests \
 *       --gtest_filter=TestBoundedLockFreeQueueBenchmark.DISABLED_BatchedDrainComparison
 */
TEST(TestBoundedLockFreeQueueBenchmark, DISABLED_BatchedDrainComparison)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 100 * 1000;
    constexpr size_t kQueueCapacity = 4096;

    const auto single_pop = RunQueueThroughputBenchmark(kProducerCount,
                                                        kConsumerCount,
                                                        kItemsPerProducer,
                                                        kQueueCapacity,
                                                        1,
                                                        std::chrono::seconds(30));
    PrintQueueBenchmarkResult("single pop drain", single_pop);
    AssertBenchmarkResultValid(single_pop);

    const auto batched_pop = RunQueueThroughputBenchmark(kProducerCount,
                                                         kConsumerCount,
                                                         kItemsPerProducer,
                                                         kQueueCapacity,
                                                         8,
                                                         std::chrono::seconds(30));
    PrintQueueBenchmarkResult("batched pop drain", batched_pop);
    AssertBenchmarkResultValid(batched_pop);

    if(single_pop.throughput_ops_per_sec > 0.0)
    {
        std::cout << std::fixed << std::setprecision(2)
                  << "\n[queue benchmark comparison] batch8_vs_batch1 throughput_ratio="
                  << batched_pop.throughput_ops_per_sec / single_pop.throughput_ops_per_sec
                  << " pop_miss_delta="
                  << static_cast<int64_t>(batched_pop.pop_misses) -
                         static_cast<int64_t>(single_pop.pop_misses)
                  << std::endl;
    }
}
