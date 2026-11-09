#include "base/log.h"
#include "base/log_async.h"
#include "base/log_file_sink.h"
#include "base/time_stamp.h"
#include "log_test_support.h"

#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <limits>
#include <thread>

using namespace kit_muduo;
using namespace log_test;
using namespace std::chrono_literals;

namespace {
// 仅替代实际 I/O；sink 状态机、定时器、eventfd 和 worker 均使用生产实现。
struct FlushProbe
{
    std::mutex mutex;
    std::condition_variable cv;
    std::string bytes;
    int attempts{0};
    int successes{0};
    int failures_left{0};
    size_t accept_limit{std::numeric_limits<size_t>::max()};
    std::thread::id sync_thread;
    std::function<void()> on_sync;

    bool waitForSync(int count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, 3s, [this, count] { return successes >= count; });
    }
};

class ProbeBackend final : public LogFileBackend
{
public:
    explicit ProbeBackend(std::shared_ptr<FlushProbe> probe) : probe_(std::move(probe)) {}
    LogBackendResult open(const std::string&) override { open_ = true; return LogBackendResult::Ok(); }
    void close() noexcept override { open_ = false; }
    bool isOpen() const noexcept override { return open_; }
    uint64_t openSize() const noexcept override
    {
        std::lock_guard<std::mutex> lock(probe_->mutex);
        return probe_->bytes.size();
    }
    uint64_t generation() const noexcept override { return 0; }
    uint64_t lastSequence() const noexcept override { return 0; }
    bool recompress(std::string) noexcept override { return false; }
    bool archiveInUse(const std::string&) const noexcept override { return false; }
    LogBackendResult rotate(const LogFileRotateRequest&, int64_t) noexcept override
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "unexpected rotation");
    }
    LogBackendResult append(std::string_view bytes) override
    {
        std::lock_guard<std::mutex> lock(probe_->mutex);
        const auto accepted = std::min(bytes.size(), probe_->accept_limit);
        probe_->bytes.append(bytes.data(), accepted);
        auto result = accepted == bytes.size() ? LogBackendResult::Ok()
            : LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "partial write");
        result.requested_bytes = bytes.size();
        result.written_bytes = accepted;
        return result;
    }
    LogBackendResult flush() noexcept override
    {
        std::lock_guard<std::mutex> lock(probe_->mutex);
        ++probe_->attempts;
        probe_->sync_thread = std::this_thread::get_id();
        if (probe_->failures_left > 0)
        {
            --probe_->failures_left;
            return LogBackendResult::Failure(LogBackendStatus::kSyncFailed, "injected sync failure");
        }
        ++probe_->successes;
        if (probe_->on_sync) probe_->on_sync();
        probe_->cv.notify_all();
        return LogBackendResult::Ok();
    }
private:
    std::shared_ptr<FlushProbe> probe_;
    bool open_{false};
};

class SinkFlushPolicyTest : public testing::Test
{
protected:
    void SetUp() override
    {
        config_.flush_threshold = 8;
        config_.flush_interval_ms = 30000;
        registry_.setConfig(config_);
        sink_ = makeSink("active.log", probe_);
        registry_.commit({{sink_->normalizedPath(), sink_}});
    }
    LogFileSink::Ptr makeSink(const std::string& name, const std::shared_ptr<FlushProbe>& probe)
    {
        auto sink = std::make_shared<LogFileSink>(&registry_, directory_.file(name),
                                                std::make_unique<ProbeBackend>(probe));
        EXPECT_TRUE(sink->reopen().ok());
        return sink;
    }
    TempDirectory directory_;
    LogFileConfig config_;
    LogFileSinkRegister registry_;
    std::shared_ptr<FlushProbe> probe_{std::make_shared<FlushProbe>()};
    LogFileSink::Ptr sink_;
};

// 测试思路：以显式单调时钟验证独立期限，追加不能延长首次期限。
// 例：A 在 100ms 写入，B 在 200ms 写入；A 再写后仍在 3100ms 到期，B 为 3200ms。
TEST(LogFlushStateTest, IndependentDeadlinesAreAnchoredAtFirstPendingByte)
{
    LogFileFlushState a, b;
    a.accept(1, 100, 3000);
    b.accept(1, 200, 3000);
    a.accept(1, 900, 3000);
    EXPECT_EQ(a.nextAttemptTime(8), 3100);
    EXPECT_EQ(b.nextAttemptTime(8), 3200);
    EXPECT_FALSE(a.shouldAutoFlush(3099, 8));
    EXPECT_TRUE(a.shouldAutoFlush(3100, 8));
    EXPECT_FALSE(b.shouldAutoFlush(3100, 8));
    a.clear();
    EXPECT_FALSE(a.nextAttemptTime(8));
    a.accept(1, 10000, 3000);
    EXPECT_EQ(a.nextAttemptTime(8), 13000);
}

// 测试思路：失败保留累计量并覆盖刷新时间；更多日志不能绕过冷却，也不能推迟重试。
// 例：8B 达阈值，100ms 失败 → 3100ms 重试；200ms 再写 20B 不立即重复同步。
TEST(LogFlushStateTest, FailedFlushPreservesBytesAndDelaysAutomaticRetry)
{
    LogFileFlushState state;
    state.accept(8, 10, 3000);
    EXPECT_TRUE(state.shouldAutoFlush(10, 8));
    state.failed(100, 3000);
    state.accept(20, 200, 3000);
    EXPECT_EQ(state.pending_sync_bytes, 28U);
    EXPECT_TRUE(state.last_flush_failed);
    EXPECT_EQ(state.next_flush_time, 3100);
    EXPECT_EQ(state.nextAttemptTime(8), 3100);
    EXPECT_FALSE(state.shouldAutoFlush(3099, 8));
    EXPECT_TRUE(state.shouldAutoFlush(3100, 8));
}

// 测试思路：分别覆盖未达和已达字节阈值的失败；重复失败从本次结束时间重新计时。
// 例：3100ms 失败 -> 6100ms；重试于 6200ms 再失败 -> 9200ms；成功清除状态后 8B 可立即触发。
TEST(LogFlushStateTest, RepeatedFailuresRescheduleAndSuccessRestoresByteTrigger)
{
    for (uint64_t bytes : {1U, 8U})
    {
        SCOPED_TRACE(bytes);
        LogFileFlushState state;
        state.accept(bytes, 100, 3000);
        state.failed(3100, 3000);
        EXPECT_EQ(state.pending_sync_bytes, bytes);
        EXPECT_EQ(state.nextAttemptTime(8), 6100);
        EXPECT_FALSE(state.shouldAutoFlush(6099, 8));
        EXPECT_TRUE(state.shouldAutoFlush(6100, 8));
        state.failed(6200, 3000);
        EXPECT_EQ(state.pending_sync_bytes, bytes);
        EXPECT_TRUE(state.last_flush_failed);
        EXPECT_EQ(state.next_flush_time, 9200);
        EXPECT_EQ(state.nextAttemptTime(8), 9200);
        EXPECT_FALSE(state.shouldAutoFlush(9199, 8));
        EXPECT_TRUE(state.shouldAutoFlush(9200, 8));
        state.clear();
        EXPECT_EQ(state.pending_sync_bytes, 0U);
        EXPECT_FALSE(state.next_flush_time);
        EXPECT_FALSE(state.last_flush_failed);
        EXPECT_FALSE(state.nextAttemptTime(8));
        state.accept(8, 9300, 3000);
        EXPECT_TRUE(state.shouldAutoFlush(9300, 8));
    }
}

// 测试思路：没有待同步数据的主动刷新失败不能给后续第一条日志遗留冷却状态。
// 例：空状态 100ms 失败不建立任务；200ms 写入 8B 达阈值仍可立即刷新。
TEST(LogFlushStateTest, EmptyFailureDoesNotDelayNextPendingCycle)
{
    LogFileFlushState state;
    state.failed(100, 3000);
    EXPECT_FALSE(state.next_flush_time);
    EXPECT_FALSE(state.last_flush_failed);
    EXPECT_FALSE(state.nextAttemptTime(8));
    state.accept(8, 200, 3000);
    EXPECT_TRUE(state.shouldAutoFlush(200, 8));
}

// 测试思路：累计正好达到以及跨过阈值均只同步一次，成功后取消旧定时任务。
// 例：3B+5B -> sync；随后 9B -> sync；不足阈值的 3B 不触发同步。
TEST_F(SinkFlushPolicyTest, ExactAndCrossedByteThresholdsClearDeadline)
{
    EXPECT_FALSE(sink_->append("abc").flush_attempted);
    ASSERT_TRUE(sink_->nextFlushTime());
    EXPECT_TRUE(sink_->append("defgh").flush_attempted);
    EXPECT_FALSE(sink_->nextFlushTime());
    EXPECT_TRUE(sink_->append("123456789").flush_attempted);
    EXPECT_EQ(probe_->attempts, 2);
    EXPECT_FALSE(sink_->nextFlushTime());
    EXPECT_FALSE(sink_->flushIfDue(std::numeric_limits<int64_t>::max()).flush_attempted);
    EXPECT_EQ(probe_->attempts, 2);
}

// 测试思路：从带等级的 LogAttr 进入真实 appender，等级仅参与过滤，不能触发 sink 同步。
// 例：INFO 被 WARN 门槛过滤；WARN/ERROR/FATAL 各写一条后共 6B，小于 8B 阈值，主动 flush 才同步 "w\ne\nf\n"。
TEST_F(SinkFlushPolicyTest, AppenderFiltersLevelsWithoutTriggeringFlush)
{
    FileAppender appender(sink_);
    appender.setFormatter("%m");
    appender.setLevel(LogLevel::WARN);
    auto logger = std::make_shared<Logger>(&LogManager::GetInstance(), "level-filter");
    const auto append = [&appender, &logger](LogLevel::Level level, const char* message) {
        auto attr = std::make_shared<LogAttr>(logger, level, logger->getName(),
            "test", __FILE__, __LINE__, 0, ::pthread_self(), ::getpid(), "test", 0);
        attr->getSS() << message;
        attr->seal();
        static_cast<LogAppender&>(appender).append(std::move(attr));
    };
    append(LogLevel::INFO, "i");
    EXPECT_TRUE(probe_->bytes.empty());
    EXPECT_FALSE(sink_->nextFlushTime());
    append(LogLevel::WARN, "w");
    append(LogLevel::ERROR, "e");
    append(LogLevel::FATAL, "f");
    EXPECT_EQ(probe_->bytes, "w\ne\nf\n");
    EXPECT_EQ(probe_->attempts, 0);
    EXPECT_TRUE(sink_->nextFlushTime());
    EXPECT_TRUE(sink_->flush().ok());
    EXPECT_EQ(probe_->attempts, 1);
    EXPECT_FALSE(sink_->nextFlushTime());
}

// 测试思路：同步错误必须携带原始原因，下一次 append 不重试，期限到达才尝试且不重写日志。
// 例：8B 首次同步 EIO，追加 1B 后仍只有一次尝试；到期同步完整 9B。
TEST_F(SinkFlushPolicyTest, SyncFailureRetriesOnDeadlineWithoutDuplicatingBytes)
{
    probe_->failures_left = 1;
    const auto failed = sink_->append("12345678");
    EXPECT_EQ(failed.status, LogFileSinkResultStatus::kSyncFailed);
    EXPECT_EQ(failed.written_bytes, 8U);
    EXPECT_EQ(failed.message, "injected sync failure");
    const auto retry_time = sink_->nextFlushTime();
    ASSERT_TRUE(retry_time);
    EXPECT_FALSE(sink_->append("9").flush_attempted);
    EXPECT_EQ(sink_->nextFlushTime(), retry_time);
    EXPECT_EQ(registry_.nextFlushTime(), retry_time);
    EXPECT_FALSE(sink_->flushIfDue(*retry_time - 1).flush_attempted);
    EXPECT_EQ(probe_->attempts, 1);
    EXPECT_TRUE(sink_->flushIfDue(*retry_time).ok());
    EXPECT_EQ(probe_->attempts, 2);
    EXPECT_EQ(probe_->bytes, "123456789");
    EXPECT_FALSE(sink_->nextFlushTime());
}

// 测试思路：主动 flush 可绕过失败冷却，并清除旧期限，后续定时检查不能重复同步。
// 例：8B 自动同步失败后立即手动 flush 成功，远期检查无任务；再写 8B 应立即同步，不残留失败标记。
TEST_F(SinkFlushPolicyTest, ManualFlushBypassesRetryCooldown)
{
    probe_->failures_left = 1;
    EXPECT_FALSE(sink_->append("12345678").ok());
    EXPECT_TRUE(sink_->flush().ok());
    EXPECT_EQ(probe_->attempts, 2);
    EXPECT_FALSE(sink_->flushIfDue(std::numeric_limits<int64_t>::max()).flush_attempted);
    const auto next = sink_->append("abcdefgh");
    EXPECT_TRUE(next.ok());
    EXPECT_TRUE(next.flush_attempted);
    EXPECT_EQ(probe_->attempts, 3);
    EXPECT_EQ(probe_->bytes, "12345678abcdefgh");
    EXPECT_FALSE(sink_->nextFlushTime());
}

// 测试思路：部分写入失败也必须记录实际接受量，保留打开状态供后续同步。
// 例：输入 6B 仅接受 2B，sink 返回 written=2，主动 flush 同步 "ab" 而不是重放整条。
TEST_F(SinkFlushPolicyTest, PartialAcceptanceRemainsPendingAfterWriteFailure)
{
    probe_->accept_limit = 2;
    const auto result = sink_->append("abcdef");
    EXPECT_EQ(result.status, LogFileSinkResultStatus::kWriteFailed);
    EXPECT_EQ(result.written_bytes, 2U);
    EXPECT_EQ(sink_->currentFileSize(), 2U);
    EXPECT_TRUE(sink_->nextFlushTime());
    EXPECT_TRUE(sink_->flush().ok());
    EXPECT_EQ(probe_->bytes, "ab");
}

// 测试思路：不同 sink 保留不同期限；用显式 now 驱动检查，避免睡眠决定先后。
// 例：A 已到期时仅同步 A；B 尚未到期不应被同一次扫描提前同步。
TEST_F(SinkFlushPolicyTest, RegisterFlushesOnlyDueSinks)
{
    ASSERT_TRUE(sink_->append("a").ok());
    const auto a_deadline = *sink_->nextFlushTime();
    config_.flush_interval_ms = 60000;
    registry_.setConfig(config_);
    auto b_probe = std::make_shared<FlushProbe>();
    auto b = makeSink("b.log", b_probe);
    ASSERT_TRUE(b->append("b").ok());
    registry_.commit({{sink_->normalizedPath(), sink_}, {b->normalizedPath(), b}});
    registry_.flushDue(a_deadline);
    EXPECT_EQ(probe_->attempts, 1);
    EXPECT_EQ(b_probe->attempts, 0);
    registry_.flushDue(*b->nextFlushTime());
    EXPECT_EQ(b_probe->attempts, 1);
}

// 测试思路：启动空闲 writer 后，从另一线程直接写入 sink；首次变脏通知必须重设 timerfd。
// 例：1B 未到字节阈值且无后续日志，真实 timerfd 在 writer 线程触发同步。
TEST_F(SinkFlushPolicyTest, IdleWriterFlushesTailWithoutAnotherLog)
{
    config_.flush_interval_ms = 20;
    registry_.setConfig(config_);
    LogAsyncDispatcher dispatcher(registry_);
    dispatcher.start();
    ASSERT_TRUE(sink_->append("x").ok());
    ASSERT_TRUE(probe_->waitForSync(1));
    ASSERT_TRUE(dispatcher.shutdown());
    dispatcher.join();
    EXPECT_NE(probe_->sync_thread, std::this_thread::get_id());
    EXPECT_EQ(probe_->attempts, 1);
    EXPECT_FALSE(sink_->nextFlushTime());
}

// 测试思路：字节触发失败后保留待同步数据，由真实 writer/timerfd 在新期限重试，无需新日志。
// 例：8B 首次同步失败，间隔 20ms；启动 writer 后自动同步成功，总尝试为 2，内容不重复。
TEST_F(SinkFlushPolicyTest, IdleWriterRetriesFailedFlushWithoutNewLogs)
{
    config_.flush_interval_ms = 20;
    registry_.setConfig(config_);
    probe_->failures_left = 1;
    ASSERT_FALSE(sink_->append("12345678").ok());
    ASSERT_TRUE(sink_->nextFlushTime());
    EXPECT_EQ(probe_->attempts, 1);
    const auto retry_time = *sink_->nextFlushTime();
    std::atomic<int64_t> sync_time{-1};
    probe_->on_sync = [&sync_time] { sync_time = TimeStamp::MonotonicNowMs(); };
    LogAsyncDispatcher dispatcher(registry_);
    dispatcher.start();
    EXPECT_TRUE(probe_->waitForSync(1));
    EXPECT_TRUE(dispatcher.shutdown());
    dispatcher.join();
    EXPECT_GE(sync_time.load(), retry_time);
    EXPECT_EQ(probe_->attempts, 2);
    EXPECT_EQ(probe_->bytes, "12345678");
    EXPECT_FALSE(sink_->nextFlushTime());
}

// 测试思路：启动已有文件即建立待同步状态；高于阈值时不等新 append 即自动刷盘。
// 例：打开包含 9B 的 active，writer 初次扫描完成同步，文件内容没有追加重复。
TEST_F(SinkFlushPolicyTest, ExistingActiveIsSyncedWithoutNewAppend)
{
    auto existing = std::make_shared<FlushProbe>();
    existing->bytes = "123456789";
    auto sink = makeSink("existing.log", existing);
    registry_.commit({{sink->normalizedPath(), sink}});
    LogAsyncDispatcher dispatcher(registry_);
    dispatcher.start();
    ASSERT_TRUE(existing->waitForSync(1));
    ASSERT_TRUE(dispatcher.shutdown());
    dispatcher.join();
    EXPECT_EQ(existing->bytes, "123456789");
    EXPECT_EQ(existing->attempts, 1);
}

// 测试思路：移除/销毁 writer 后仍允许 sink 改变期限，再装配新 writer，旧通知不能访问悬空对象。
// 例：第一代 dispatcher 停止后写 1B，第二代首次扫描正常处理尾批。
TEST_F(SinkFlushPolicyTest, WakeupCanBeDetachedAndRebound)
{
    config_.flush_interval_ms = 10;
    registry_.setConfig(config_);
    {
        LogAsyncDispatcher first(registry_);
        first.start();
        ASSERT_TRUE(first.shutdown());
    }
    ASSERT_TRUE(sink_->append("x").ok());
    LogAsyncDispatcher second(registry_);
    second.start();
    ASSERT_TRUE(probe_->waitForSync(1));
    EXPECT_TRUE(second.shutdown());
}

class BudgetAppender final : public LogAppender
{
public:
    void append(const std::string&) override
    {
        const int n = ++count;
        if (n == 1) first.enterAndWait();
        if (n == 256) boundary.enterAndWait();
        {
            std::lock_guard<std::mutex> lock(mutex);
            completed = n;
        }
        cv.notify_all();
    }
    bool waitForAll()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, 3s, [this] { return completed == 600; });
    }
    std::atomic<int> count{0};
    Gate first, boundary;
    std::mutex mutex;
    std::condition_variable cv;
    int completed{0};
};

LogAttr::Ptr MakeFlushAttr(const Logger::Ptr& logger)
{
    auto attr = std::make_shared<LogAttr>(logger, LogLevel::INFO, logger->getName(),
        "test", __FILE__, __LINE__, 0, ::pthread_self(), ::getpid(), "test", 0);
    attr->getSS() << "x";
    attr->seal();
    return attr;
}

// 测试思路：用 Gate 在第 1 和第 256 条暂停，保证事件通知已被合并但队列仍有余量。
// 例：已排队 600 条；第 256 条处发布到期 sink，放行后必须先同步且最终消费全部 600 条。
TEST_F(SinkFlushPolicyTest, BusyQueueServicesDeadlinesAndRetainsUnnotifiedRemainder)
{
    auto appender = std::make_shared<BudgetAppender>();
    auto logger = std::make_shared<Logger>(&LogManager::GetInstance(), "budget");
    logger->addAppender(appender);
    LogAsyncDispatcher dispatcher(registry_);
    ReleaseGateOnExit release_first(appender->first), release_boundary(appender->boundary);
    dispatcher.start();
    ASSERT_EQ(dispatcher.submit(MakeFlushAttr(logger)).status, LogSubmitStatus::kQueued);
    ASSERT_TRUE(appender->first.waitForEntered());
    for (int i = 1; i < 600; ++i)
        ASSERT_EQ(dispatcher.submit(MakeFlushAttr(logger)).status, LogSubmitStatus::kQueued);
    appender->first.release();
    ASSERT_TRUE(appender->boundary.waitForEntered());
    auto pending = std::make_shared<FlushProbe>();
    pending->bytes = "123456789";
    auto consumed_at_sync = std::make_shared<std::atomic<int>>(-1);
    pending->on_sync = [appender, consumed_at_sync] { *consumed_at_sync = appender->count.load(); };
    auto pending_sink = makeSink("due.log", pending);
    registry_.commit({{pending_sink->normalizedPath(), pending_sink}});
    appender->boundary.release();
    ASSERT_TRUE(pending->waitForSync(1));
    EXPECT_EQ(consumed_at_sync->load(), 256);
    // shutdown 的额外通知不能掩盖队列剩余数据无人唤醒的问题。
    EXPECT_TRUE(appender->waitForAll());
    ASSERT_TRUE(dispatcher.shutdown());
    dispatcher.join();
    EXPECT_EQ(appender->count.load(), 600);
    EXPECT_EQ(pending->attempts, 1);
}
} // namespace
