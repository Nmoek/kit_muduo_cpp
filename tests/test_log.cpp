/**
 * @file test_log.cpp
 * @brief 日志组件专项测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-05
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log.h"
#include "base/log_async.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <optional>
#include <pthread.h>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <unistd.h>
#include <vector>

using namespace kit_muduo;

namespace {

class TempLogFile
{
public:
    explicit TempLogFile(const std::string &case_name)
        :path_("/tmp/kit_muduo_test_log_" + std::to_string(::getpid()) + "_" + case_name + "_test.log")
    {
        ::unlink(path_.c_str());
    }

    ~TempLogFile()
    {
        ::unlink(path_.c_str());
    }

    const std::string& path() const
    {
        return path_;
    }

private:
    std::string path_;
};

class TempLogDirectory
{
public:
    explicit TempLogDirectory(const std::string& case_name)
        :path_(std::filesystem::temp_directory_path()
            / ("kit_muduo_test_log_" + std::to_string(::getpid())
                + "_" + case_name + "_dir"))
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_);
    }

    ~TempLogDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ScopedTimezone
{
public:
    explicit ScopedTimezone(const char *timezone)
    {
        const char *old_timezone = ::getenv("TZ");
        if(old_timezone != nullptr)
        {
            old_timezone_ = old_timezone;
        }

        ::setenv("TZ", timezone, 1);
        ::tzset();
    }

    ~ScopedTimezone()
    {
        if(old_timezone_.has_value())
        {
            ::setenv("TZ", old_timezone_->c_str(), 1);
        }
        else
        {
            ::unsetenv("TZ");
        }
        ::tzset();
    }

private:
    std::optional<std::string> old_timezone_;
};

std::string ReadFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void AppendAttr(LogAppender& appender, LogAttr::Ptr attr)
{
    appender.LogAppender::append(std::move(attr));
}

LogAttr::Ptr MakeLogAttr(const std::string &content, uint64_t timestamp = 0)
{
    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_log");
    auto attr = std::make_shared<LogAttr>(
        logger,
        LogLevel::INFO,
        logger->getName(),
        "log_test",
        __FILE__,
        __LINE__,
        0,
        pthread_self(),
        ::getpid(),
        "test_log",
        timestamp);
    attr->getSS() << content;
    return attr;
}

LogAttr::Ptr MakeLoggerAttr(
    const Logger::Ptr& logger,
    LogLevel::Level level,
    const std::string& content)
{
    auto attr = std::make_shared<LogAttr>(
        logger,
        level,
        logger->getName(),
        "log_test",
        __FILE__,
        __LINE__,
        0,
        pthread_self(),
        ::getpid(),
        "test_log",
        0);
    attr->getSS() << content;
    return attr;
}

class CountingAppender final : public LogAppender
{
public:
    void append(const std::string&,
        LogLevel::Level level) override
    {
        if(level >= getLevel())
        {
            count_.fetch_add(1, std::memory_order_release);
            cv_.notify_all();
        }
    }

    int count() const noexcept
    {
        return count_.load(std::memory_order_relaxed);
    }

    bool waitForCount(int expected, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(wait_mtx_);
        return cv_.wait_for(lock, timeout, [this, expected] {
            return count_.load(std::memory_order_acquire) >= expected;
        });
    }

private:
    std::atomic_int count_{0};
    std::mutex wait_mtx_;
    std::condition_variable cv_;
};

class BlockingAppender final : public LogAppender
{
public:
    void append(const std::string&,
        LogLevel::Level) override
    {
        blockUntilReleased();
    }

    bool waitUntilEntered(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(gate_mtx_);
        return entered_cv_.wait_for(
            lock,
            timeout,
            [this] { return entered_; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(gate_mtx_);
        released_ = true;
        release_cv_.notify_all();
    }

    int count() const noexcept
    {
        return count_.load(std::memory_order_relaxed);
    }

    bool waitForCount(int expected, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(gate_mtx_);
        return entered_cv_.wait_for(
            lock,
            timeout,
            [this, expected] {
                return count_.load(std::memory_order_acquire) >= expected;
            });
    }

private:
    void blockUntilReleased()
    {
        std::unique_lock<std::mutex> lock(gate_mtx_);
        entered_ = true;
        entered_cv_.notify_all();
        release_cv_.wait(lock, [this] { return released_; });
        count_.fetch_add(1, std::memory_order_relaxed);
        entered_cv_.notify_all();
    }
    std::mutex gate_mtx_;
    std::condition_variable entered_cv_;
    std::condition_variable release_cv_;
    bool entered_{false};
    bool released_{false};
    std::atomic_int count_{0};
};

LogConfig MakeManagerTestConfig()
{
    LogConfig config;
    LoggerConfig root;
    root.name = "root";
    root.level = LogLevel::DEBUG;
    root.appenders.push_back(LogAppenderConfig{
        .type = LogAppenderType::kStdout,
        .level = LogLevel::DEBUG,
    });
    config.loggers.push_back(std::move(root));
    return config;
}

class ScopedDefaultLogConfig
{
public:
    ~ScopedDefaultLogConfig()
    {
        try
        {
            LogManager::GetInstance().applyConfig(
                MakeManagerTestConfig());
        }
        catch(...)
        {
        }
    }
};

} // namespace

/*
测试思路：通过默认 LogFileSinkRegister 获取 sink，再交给 FileAppender，验证
新接口下小日志不会主动触发阈值 flush，最后一个 sink 引用析构时文件流正常关闭。

路径图：FileAppender -> shared LogFileSink -> ofstream。
示例：append("first") -> appender 析构 -> 文件最终为 "first"。
*/
TEST(TestLog, FileAppenderDefaultWriteMaxSizeDoesNotFlushSmallFirstWrite)
{
    TempLogFile file("default_threshold");
    LogFileSinkRegister registry;

    {
        FileAppender appender(registry.acquire(file.path()));
        appender.setFormatter("%m");

        AppendAttr(appender, MakeLogAttr("first"));

        ASSERT_EQ(ReadFile(file.path()), "first\n");
    }

    ASSERT_EQ(ReadFile(file.path()), "first\n");
}

/*
测试思路：把 flush_threshold 配置到 registry，而不是单个 Appender，验证共享
sink 按累计格式化结果触发 flush。

状态图：0 --"abc"--> 3 --"defgh"--> 8/flush -> 0 --"z"--> 1。
示例：前两条累计 8 字节后文件可读为 "abcdefgh"。
*/
TEST(TestLog, FileAppenderFlushesAfterCumulativeConfiguredBytes)
{
    TempLogFile file("cumulative_threshold");
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.flush_threshold = 8;
    registry.setConfig(config);

    {
        FileAppender appender(registry.acquire(file.path()));
        appender.setFormatter("%m");

        AppendAttr(appender, MakeLogAttr("abc"));
        ASSERT_EQ(ReadFile(file.path()), "abc\n");

        AppendAttr(appender, MakeLogAttr("defgh"));
        ASSERT_EQ(ReadFile(file.path()), "abc\ndefgh\n");

        AppendAttr(appender, MakeLogAttr("z"));
        ASSERT_EQ(ReadFile(file.path()), "abc\ndefgh\nz\n");
    }

    ASSERT_EQ(ReadFile(file.path()), "abc\ndefgh\nz\n");
}

/*
测试思路：配置 0 字节阈值，验证物理策略来自 registry，并覆盖每次 append
都立即 flush 的边界行为。

状态图：append -> bytes >= 0 -> flush。
示例：连续 append("a"), append("b") 后分别立即读到 "a"、"ab"。
*/
TEST(TestLog, FileAppenderZeroWriteMaxSizeFlushesEveryWrite)
{
    TempLogFile file("zero_threshold");
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.flush_threshold = 0;
    registry.setConfig(config);

    FileAppender appender(registry.acquire(file.path()));
    appender.setFormatter("%m");

    AppendAttr(appender, MakeLogAttr("a"));
    ASSERT_EQ(ReadFile(file.path()), "a\n");

    AppendAttr(appender, MakeLogAttr("b"));
    ASSERT_EQ(ReadFile(file.path()), "a\nb\n");
}

/*
测试思路：同一个规范化绝对路径必须只对应一个 LogFileSink；不同路径必须获得
不同 sink，从对象身份上验证 registry 的资源划分边界。

资源图：path A -> sink A <- acquire(path A)，path B -> sink B。
示例：两次 acquire(file_a) 指针相同，acquire(file_b) 指针不同。
*/
TEST(TestLog, FileSinkRegisterSharesOnlyTheSamePath)
{
    TempLogFile file_a("registry_same_a");
    TempLogFile file_b("registry_same_b");
    LogFileSinkRegister registry;

    auto first = registry.acquire(file_a.path());
    auto second = registry.acquire(file_a.path());
    auto other = registry.acquire(file_b.path());

    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);
    EXPECT_EQ(first->normalizedPath(),
        std::filesystem::weakly_canonical(file_a.path()).string());
}

/*
测试思路：相对路径、`.` 和 `..` 只是同一文件的不同 pathname，归一化后必须
命中同一个 registry key，避免创建多个 ofstream。

路径图：target.log == ./target.log == nested/../target.log == relative(target.log)。
示例：四种路径 acquire 后得到完全相同的 shared_ptr。
*/
TEST(TestLog, FileSinkRegisterNormalizesEquivalentPaths)
{
    TempLogDirectory directory("normalize");
    const auto nested = directory.path() / "nested";
    std::filesystem::create_directories(nested);

    const auto target = directory.path() / "target.log";
    const auto dotted = directory.path() / "." / "target.log";
    const auto parent = nested / ".." / "target.log";
    const auto relative = std::filesystem::relative(
        target,
        std::filesystem::current_path());

    LogFileSinkRegister registry;
    auto canonical_sink = registry.acquire(target.string());

    EXPECT_EQ(registry.acquire(dotted.string()), canonical_sink);
    EXPECT_EQ(registry.acquire(parent.string()), canonical_sink);
    EXPECT_EQ(registry.acquire(relative.string()), canonical_sink);
}

/*
测试思路：registry 只持有 weak_ptr。最后一个使用者释放后 sink 应被回收；再次
acquire 创建新 sink，并从原文件尾部继续追加。

生命周期图：sink#1 --last shared_ptr reset--> expired --acquire--> sink#2。
示例：sink#1 写 "first"，sink#2 写 "second"，最终文件为 "firstsecond"。
*/
TEST(TestLog, FileSinkRegisterRecreatesExpiredSinkAndKeepsAppending)
{
    TempLogFile file("registry_recycle");
    LogFileSinkRegister registry;
    std::weak_ptr<LogFileSink> previous;

    {
        auto sink = registry.acquire(file.path());
        previous = sink;
        ASSERT_TRUE(sink->append(
            "first", LogLevel::INFO).ok());
        ASSERT_TRUE(sink->flush().ok());
    }

    EXPECT_TRUE(previous.expired());

    auto replacement = registry.acquire(file.path());
    ASSERT_TRUE(replacement->append(
        "second", LogLevel::INFO).ok());
    ASSERT_TRUE(replacement->flush().ok());

    EXPECT_EQ(ReadFile(file.path()), "firstsecond");
}

/*
测试思路：两个 FileAppender 共享同一个 sink。销毁其中一个只能释放自身引用，
不能关闭另一个 Appender 正在使用的物理文件状态。

生命周期图：Appender A --destroy--> sink <- Appender B --append--> file。
示例：A 写 "first\n" 后析构，B 继续写 "second\n"，两条记录都存在。
*/
TEST(TestLog, SharedFileSinkSurvivesOneAppenderDestruction)
{
    TempLogFile file("shared_appender_lifetime");
    LogFileSinkRegister registry;
    auto sink = registry.acquire(file.path());
    auto first = std::make_unique<FileAppender>(sink);
    FileAppender second(sink);
    first->setFormatter("%m%n");
    second.setFormatter("%m%n");

    AppendAttr(*first, MakeLogAttr("first"));
    first.reset();
    AppendAttr(second, MakeLogAttr("second"));
    ASSERT_TRUE(sink->flush().ok());

    EXPECT_EQ(ReadFile(file.path()), "first\nsecond\n");
}

/*
测试思路：多个线程各自持有 FileAppender，但都通过 registry 获取同一路径 sink。
sink 锁必须覆盖整条最终字符串写入，使记录数量正确且内容不交错。

并发图：thread 0..3 -> private FileAppender -> shared LogFileSink::mtx -> file。
示例：4 线程各写 200 条 `t<id>-<seq>`，最终得到 800 条唯一完整记录。
*/
TEST(TestLog, SharedFileSinkPreservesConcurrentRecordBoundaries)
{
    constexpr int kThreads = 4;
    constexpr int kRecordsPerThread = 200;

    TempLogFile file("shared_concurrent");
    LogFileSinkRegister registry;
    auto sink = registry.acquire(file.path());
    std::vector<std::thread> threads;

    for(int thread_id = 0; thread_id < kThreads; ++thread_id)
    {
        threads.emplace_back([&registry, &file, thread_id] {
            FileAppender appender(registry.acquire(file.path()));
            appender.setFormatter("%m%n");

            for(int sequence = 0;
                sequence < kRecordsPerThread;
                ++sequence)
            {
                AppendAttr(appender, MakeLogAttr(
                    "t" + std::to_string(thread_id)
                    + "-" + std::to_string(sequence)));
            }
        });
    }

    for(auto& thread : threads)
    {
        thread.join();
    }
    ASSERT_TRUE(sink->flush().ok());

    std::ifstream input(file.path());
    std::unordered_set<std::string> records;
    std::string record;
    size_t record_count = 0;
    while(std::getline(input, record))
    {
        ++record_count;
        records.emplace(record);
    }

    EXPECT_EQ(record_count,
        static_cast<size_t>(kThreads * kRecordsPerThread));
    EXPECT_EQ(records.size(), record_count);
    for(int thread_id = 0; thread_id < kThreads; ++thread_id)
    {
        for(int sequence = 0;
            sequence < kRecordsPerThread;
            ++sequence)
        {
            EXPECT_EQ(records.count(
                "t" + std::to_string(thread_id)
                + "-" + std::to_string(sequence)), 1U);
        }
    }
}

/*
测试思路：acquire 已有文件时必须恢复 current size；reopen 关闭并重新打开同一
文件后也必须重新计算大小，并继续使用 append 语义。

状态图：existing(4 B) -> append(4 B) -> reopen/current=8 B -> append(4 B)。
示例："seed" + "-one" + "-two"，最终大小和内容都为 12 字节。
*/
TEST(TestLog, FileSinkRestoresSizeAndAppendsAcrossReopen)
{
    TempLogFile file("restore_size");
    {
        std::ofstream output(file.path(), std::ios::binary);
        output << "seed";
    }

    LogFileSinkRegister registry;
    auto sink = registry.acquire(file.path());
    ASSERT_EQ(sink->currentFileSize(), 4U);

    ASSERT_TRUE(sink->append(
        "-one", LogLevel::INFO).ok());
    ASSERT_TRUE(sink->flush().ok());
    EXPECT_EQ(sink->currentFileSize(), 8U);

    ASSERT_TRUE(sink->reopen().ok());
    EXPECT_EQ(sink->currentFileSize(), 8U);

    ASSERT_TRUE(sink->append(
        "-two", LogLevel::INFO).ok());
    ASSERT_TRUE(sink->flush().ok());
    EXPECT_EQ(sink->currentFileSize(), 12U);
    EXPECT_EQ(ReadFile(file.path()), "seed-one-two");
}

/*
测试思路：registry 必须在创建 sink 前拒绝空路径和目录路径，确保不会把目录
误当作普通日志文件。

示例：acquire("") 抛 invalid_argument；acquire(existing_directory) 抛
runtime_error。
*/
TEST(TestLog, FileSinkRegisterRejectsInvalidFilePaths)
{
    TempLogDirectory directory("invalid_path");
    LogFileSinkRegister registry;

    EXPECT_THROW(registry.acquire(""), std::runtime_error);
    EXPECT_EQ(registry.acquire(directory.path().string()), nullptr);
}

/*
测试思路：构造固定 Unix 毫秒的 LogAttr，只格式化 %d 和 %m，验证日志
formatter 的日期项与 TimeStamp::toLogString 完全一致，毫秒只出现一次。

示例：1750856400123 + "payload" -> "2025-06-25 21:00:00.123|payload"。
*/
TEST(TestLog, DateFormatterUsesFixedUtcPlusEightAndMilliseconds)
{
    constexpr uint64_t kTimestamp = 1750856400123ULL;
    auto attr = MakeLogAttr("payload", kTimestamp);
    LogFormatter formatter("%d|%m");

    EXPECT_EQ(
        formatter.format(attr),
        "2025-06-25 21:00:00.123|payload");
    EXPECT_EQ(
        formatter.format(attr).find(".123.123"),
        std::string::npos);
}

/*
测试思路：切换 TZ 后重复格式化同一个固定日志属性，验证 DateTimeFormatItem
不调用主机 localtime_r，日志仍固定显示 UTC+08:00。

示例：TZ=UTC、TZ=Asia/Shanghai、TZ=America/Los_Angeles 的结果都相同。
*/
TEST(TestLog, DateFormatterIgnoresProcessTimezone)
{
    constexpr uint64_t kTimestamp = 1750856400123ULL;
    const std::vector<const char *> timezones{
        "UTC",
        "Asia/Shanghai",
        "America/Los_Angeles",
    };

    for(const char *timezone : timezones)
    {
        ScopedTimezone scoped_timezone(timezone);
        SCOPED_TRACE(timezone);

        auto attr = MakeLogAttr("payload", kTimestamp);
        LogFormatter formatter("%d");
        EXPECT_EQ(formatter.format(attr), "2025-06-25 21:00:00.123");
    }
}

/*
测试思路：给独立 Logger 添加计数 Appender，验证 OwnAppenders 状态按 Logger
级别过滤；删除最后一个 Appender 后必须进入 Muted，所有级别都被拒绝。public
log() 还应保留提交前的安全过滤。

示例：
  DEBUG --(Logger=INFO)--> 丢弃
  ERROR --(Logger=INFO)--> count=1
  delAppender(last) -> FATAL 也丢弃，count 仍为 1
*/
TEST(TestLog, LoggerOwnAppenderFiltersAndLastRemovalMutes)
{
    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_own_appender");
    auto appender = std::make_shared<CountingAppender>();
    logger->setLevel(LogLevel::INFO);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    EXPECT_FALSE(logger->shouldLog(LogLevel::DEBUG));
    EXPECT_TRUE(logger->shouldLog(LogLevel::INFO));

    logger->log(MakeLoggerAttr(logger, LogLevel::DEBUG, "filtered"));
    logger->log(MakeLoggerAttr(logger, LogLevel::ERROR, "accepted"));
    EXPECT_EQ(appender->count(), 1);

    logger->delAppender(appender);
    EXPECT_FALSE(logger->shouldLog(LogLevel::FATAL));

    logger->log(MakeLoggerAttr(logger, LogLevel::FATAL, "muted"));
    EXPECT_EQ(appender->count(), 1);
}

/*
测试思路：日志宏先执行 shouldLog()，通过后由 LogAttrWrap 析构将 seal 后的
LogAttr 提交到异步队列。测试在有界时间内等待 writer 派发，计数必须只增加一次，
证明宏路径能够异步派发且没有重复提交。

路径图：KIT_DEBUG -> LogAttrWrap -> dispatcher -> worker -> CountingAppender。
示例：KIT_DEBUG(logger) << "once" -> 1 秒内 count 从 0 变为 1。
*/
TEST(TestLog, LogMacroDispatchesExactlyOnceThroughLogAttrWrap)
{
    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_macro_dispatch");
    auto appender = std::make_shared<CountingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    KIT_DEBUG(logger, "log_test") << "once";

    ASSERT_TRUE(appender->waitForCount(
        1, std::chrono::milliseconds(1000)));
    EXPECT_EQ(appender->count(), 1);
}

/*
测试思路：直接创建独立 LogAsyncDispatcher，提交已经 seal 的 LogAttr，验证
submit() 返回 queued，后台 worker 最终调用 Logger -> Appender，且 shutdown 能够
在队列排空后完成回收。

路径图：sealed LogAttr -> dispatcher -> MPMC -> worker -> Logger -> Appender。
示例：提交一条 "async-once"，1 秒内计数从 0 变为 1。
*/
TEST(TestLog, AsyncDispatcherQueuesSealedAttrAndDrainsWorker)
{
    LogAsyncConfig config;
    config.queue_capacity = 8;
    config.max_queue_bytes = 1024;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_dispatcher");
    auto appender = std::make_shared<CountingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    auto attr = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "async-once");
    ASSERT_TRUE(attr->seal());

    const auto submit = dispatcher.submit(attr);
    EXPECT_EQ(submit.status, LogSubmitStatus::kQueued);
    EXPECT_EQ(submit.bytes, attr->getContent().size());
    ASSERT_TRUE(appender->waitForCount(
        1,
        std::chrono::milliseconds(1000)));
    EXPECT_EQ(appender->count(), 1);

    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：LogAsyncDispatcher 直接复用 BoundedLockFreeQueue，队列容量小于 2
不满足 MPMC 序列号协议，因此 dispatcher 构造阶段必须拒绝 0 和 1，而不是
启动一个容量不合法的异步 worker。

边界图：queue_capacity <= 1 -> invalid_argument；queue_capacity = 2 -> 正常启动。
*/
TEST(TestLog, AsyncDispatcherRejectsCapacityBelowTwo)
{
    LogAsyncConfig invalid_zero;
    invalid_zero.queue_capacity = 0;
    EXPECT_THROW(
        LogAsyncDispatcher dispatcher(invalid_zero),
        std::invalid_argument);

    LogAsyncConfig invalid_one;
    invalid_one.queue_capacity = 1;
    EXPECT_THROW(
        LogAsyncDispatcher dispatcher(invalid_one),
        std::invalid_argument);

    LogAsyncConfig valid;
    valid.queue_capacity = 2;
    LogAsyncDispatcher dispatcher(valid);
    EXPECT_EQ(dispatcher.capacity(), 2U);
}

/*
测试思路：连续提交多条 sealed LogAttr，验证 worker 被一次唤醒后持续 tryPop
直到队列为空，所有记录都能被同一个 Appender 处理，避免 waitPop 单条消费语义。

路径图：N 条 LogAttr -> 一次/多次 semaphore 唤醒 -> while(tryPop) -> Appender。
示例：32 条记录全部完成后 count 必须等于 32。
*/
TEST(TestLog, AsyncDispatcherDrainsMultipleQueuedAttrs)
{
    constexpr int kRecords = 32;

    LogAsyncConfig config;
    config.queue_capacity = 64;
    config.max_queue_bytes = 16 * 1024;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_batch");
    auto appender = std::make_shared<CountingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    for(int index = 0; index < kRecords; ++index)
    {
        auto attr = MakeLoggerAttr(
            logger,
            LogLevel::INFO,
            "async-" + std::to_string(index));
        ASSERT_TRUE(attr->seal());
        ASSERT_EQ(
            dispatcher.submit(std::move(attr)).status,
            LogSubmitStatus::kQueued);
    }

    ASSERT_TRUE(appender->waitForCount(
        kRecords,
        std::chrono::milliseconds(1000)));
    EXPECT_EQ(appender->count(), kRecords);
    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：不提前等待 appender 计数，直接在队列中留下多条 sealed LogAttr 后调用
shutdown()。shutdown 必须唤醒 worker、排空队列并等待 drain 完成；再次调用 shutdown
应保持幂等，不重复启动或回收线程。

生命周期图：started -> queued records -> shutdown/drain -> worker joined -> shutdown again。
示例：16 条记录在 shutdown 返回前全部完成，第二次 shutdown 仍返回 true。
*/
TEST(TestLog, AsyncDispatcherShutdownDrainsQueuedRecordsAndIsIdempotent)
{
    constexpr int kRecords = 16;

    LogAsyncConfig config;
    config.queue_capacity = 32;
    config.max_queue_bytes = 4096;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_shutdown_drain");
    auto appender = std::make_shared<CountingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    for(int index = 0; index < kRecords; ++index)
    {
        auto attr = MakeLoggerAttr(
            logger,
            LogLevel::INFO,
            "shutdown-" + std::to_string(index));
        ASSERT_TRUE(attr->seal());
        ASSERT_EQ(
            dispatcher.submit(std::move(attr)).status,
            LogSubmitStatus::kQueued);
    }

    EXPECT_TRUE(dispatcher.shutdown());
    EXPECT_EQ(appender->count(), kRecords);
    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：dispatcher shutdown 后不再接受新的 sealed LogAttr，提交结果必须明确
返回 stopped，不能伪装成 queued 或成功。

状态图：started -> shutdown -> stopped submission。
示例：shutdown 后 submit("late") -> LogSubmitStatus::kStopped。
*/
TEST(TestLog, AsyncDispatcherRejectsSubmissionAfterShutdown)
{
    LogAsyncConfig config;
    config.queue_capacity = 4;
    config.max_queue_bytes = 1024;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();
    ASSERT_TRUE(dispatcher.shutdown());

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_stopped");
    auto attr = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "late");
    ASSERT_TRUE(attr->seal());

    EXPECT_EQ(
        dispatcher.submit(std::move(attr)).status,
        LogSubmitStatus::kStopped);
}

/*
测试思路：将 dispatcher 的总字节预算设置为 4 字节，提交 5 字节记录，验证
记录不会进入队列，并按当前等级策略返回 dropped；返回的 bytes 必须保留原记录
大小，便于调用方统计被丢弃的数据量。

路径图：sealed LogAttr(5B) -> byte budget reject -> kDropped(5B)。
*/
TEST(TestLog, AsyncDispatcherDropsRecordExceedingByteBudget)
{
    LogAsyncConfig config;
    config.queue_capacity = 8;
    config.max_queue_bytes = 4;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_budget");
    auto attr = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "12345");
    ASSERT_TRUE(attr->seal());

    const auto submit = dispatcher.submit(std::move(attr));
    EXPECT_EQ(submit.status, LogSubmitStatus::kDropped);
    EXPECT_EQ(submit.bytes, 5U);
    EXPECT_EQ(dispatcher.queueSize(), 0U);
    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：submit() 的输入契约要求属性非空且已经 seal。分别提交未 seal 属性和
空指针，验证两者都在入口直接返回 stopped，不触发队列预算或等级等待。

状态图：invalid attr -> kStopped；null attr -> kStopped。
*/
TEST(TestLog, AsyncDispatcherRejectsInvalidAttributes)
{
    LogAsyncConfig config;
    config.queue_capacity = 8;
    config.max_queue_bytes = 1024;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_invalid_attr");
    auto unsealed = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "unsealed");

    EXPECT_EQ(
        dispatcher.submit(unsealed).status,
        LogSubmitStatus::kStopped);
    EXPECT_EQ(
        dispatcher.submit(nullptr).status,
        LogSubmitStatus::kStopped);
    EXPECT_EQ(dispatcher.queueSize(), 0U);
    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：多个生产线程同时向同一个 dispatcher 提交 sealed 属性，单 worker 必须
完整消费所有记录，验证 MPMC 入队、semaphore 唤醒和批量 tryPop 的组合行为。

并发图：4 producers -> MPMC -> 1 worker -> CountingAppender。
示例：4 个线程各提交 50 条，最终计数为 200 且无提交失败。
*/
TEST(TestLog, AsyncDispatcherConsumesConcurrentProducerSubmissions)
{
    constexpr int kThreads = 4;
    constexpr int kRecordsPerThread = 50;

    LogAsyncConfig config;
    config.queue_capacity = 512;
    config.max_queue_bytes = 128 * 1024;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_concurrent");
    auto appender = std::make_shared<CountingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    std::atomic_int submit_failures{0};
    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for(int thread_id = 0; thread_id < kThreads; ++thread_id)
    {
        producers.emplace_back([
            &dispatcher,
            &logger,
            &submit_failures,
            thread_id] {
            for(int sequence = 0;
                sequence < kRecordsPerThread;
                ++sequence)
            {
                auto attr = MakeLoggerAttr(
                    logger,
                    LogLevel::INFO,
                    "producer-" + std::to_string(thread_id)
                        + "-" + std::to_string(sequence));
                if(!attr->seal())
                {
                    submit_failures.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    continue;
                }
                if(dispatcher.submit(std::move(attr)).status
                    != LogSubmitStatus::kQueued)
                {
                    submit_failures.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }
            }
        });
    }

    for(auto& producer : producers)
    {
        producer.join();
    }

    constexpr int kExpected = kThreads * kRecordsPerThread;
    EXPECT_EQ(submit_failures.load(std::memory_order_relaxed), 0);
    ASSERT_TRUE(appender->waitForCount(
        kExpected,
        std::chrono::milliseconds(2000)));
    EXPECT_EQ(appender->count(), kExpected);
    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：用 BlockingAppender 卡住 worker 正在处理的第一条记录，再按队列实际
归一化容量填满剩余槽位，确保探测提交发生时队列持续满载。验证固定满载策略：
DEBUG/INFO 不等待，WARN 最多等待 10 ms，ERROR/FATAL 最多等待 50 ms，超时后
统一返回 dropped。

时序图：first -> worker(阻塞)；queued[capacity] -> queue(满)；probe(level) -> 等待/丢弃。
*/
TEST(TestLog, AsyncDispatcherAppliesLevelBasedFullQueueWaits)
{
    struct Case
    {
        LogLevel::Level level;
        std::chrono::milliseconds expected_wait;
    };

    const std::vector<Case> cases{
        {LogLevel::DEBUG, std::chrono::milliseconds::zero()},
        {LogLevel::INFO, std::chrono::milliseconds::zero()},
        {LogLevel::WARN, std::chrono::milliseconds(10)},
        {LogLevel::ERROR, std::chrono::milliseconds(50)},
        {LogLevel::FATAL, std::chrono::milliseconds(50)},
    };

    for(const auto& test_case : cases)
    {
        SCOPED_TRACE(static_cast<int>(test_case.level));

        LogAsyncConfig config;
        config.queue_capacity = 3;
        config.max_queue_bytes = 1024;
        config.stop_drain_timeout_ms = 1000;

        LogAsyncDispatcher dispatcher(config);
        dispatcher.start();
        // BoundedLockFreeQueue 会把配置容量向上归一化为 2 的幂；满载判断必须
        // 使用实际容量，不能把 config.queue_capacity(3) 当作槽位上限。
        ASSERT_EQ(dispatcher.capacity(), 4U);

        auto logger = std::make_shared<Logger>(
            &LogManager::GetInstance(), "test_async_full_queue");
        auto appender = std::make_shared<BlockingAppender>();
        logger->setLevel(LogLevel::DEBUG);
        appender->setLevel(LogLevel::DEBUG);
        logger->addAppender(appender);

        auto first = MakeLoggerAttr(
            logger,
            LogLevel::INFO,
            "blocking-first");
        ASSERT_TRUE(first->seal());
        ASSERT_EQ(
            dispatcher.submit(std::move(first)).status,
            LogSubmitStatus::kQueued);
        ASSERT_TRUE(appender->waitUntilEntered(
            std::chrono::milliseconds(1000)));

        // 第一条正在 worker 中执行，剩余记录填满归一化后的 4 槽队列。
        for(size_t index = 0; index < dispatcher.capacity(); ++index)
        {
            auto queued = MakeLoggerAttr(
                logger,
                LogLevel::INFO,
                "queued-" + std::to_string(index));
            ASSERT_TRUE(queued->seal());
            ASSERT_EQ(
                dispatcher.submit(std::move(queued)).status,
                LogSubmitStatus::kQueued);
        }

        auto probe = MakeLoggerAttr(
            logger,
            test_case.level,
            "probe");
        ASSERT_TRUE(probe->seal());

        const auto begin = std::chrono::steady_clock::now();
        const auto submit = dispatcher.submit(std::move(probe));
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - begin);

        EXPECT_EQ(submit.status, LogSubmitStatus::kDropped);
        if(test_case.expected_wait == std::chrono::milliseconds::zero())
        {
            EXPECT_LT(elapsed.count(), 20);
        }
        else
        {
            EXPECT_GE(
                elapsed.count(),
                test_case.expected_wait.count() - 3);
            EXPECT_LT(
                elapsed.count(),
                test_case.expected_wait.count() + 200);
        }

        appender->release();
        EXPECT_TRUE(dispatcher.shutdown());
    }
}

/*
测试思路：先让 worker 卡在第一条日志，再按队列实际容量填满剩余槽位；下一条
ERROR 日志必须进入 not_full_cv_ 等待。释放第一条后，worker 弹出第二条并通知
等待生产者，探测日志应在 50ms 等级等待窗口内成功入队，而不是被误判为 dropped。

时序图：first -> worker(阻塞)；queued[capacity] -> queue(满)；probe -> wait(not_full_cv_)
-> release first -> pop/notify -> probe queued。
*/
TEST(TestLog, AsyncDispatcherWakesWaitingProducerAfterCapacityReleased)
{
    LogAsyncConfig config;
    // 当前 MPMC 队列契约要求容量至少为 2。
    config.queue_capacity = 2;
    config.max_queue_bytes = 1024;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_space_wakeup");
    auto appender = std::make_shared<BlockingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    auto first = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "blocking-first");
    ASSERT_TRUE(first->seal());
    ASSERT_EQ(
        dispatcher.submit(std::move(first)).status,
        LogSubmitStatus::kQueued);
    ASSERT_TRUE(appender->waitUntilEntered(
        std::chrono::milliseconds(1000)));

    for(size_t index = 0; index < dispatcher.capacity(); ++index)
    {
        auto queued = MakeLoggerAttr(
            logger,
            LogLevel::INFO,
            "queued-" + std::to_string(index));
        ASSERT_TRUE(queued->seal());
        ASSERT_EQ(
            dispatcher.submit(std::move(queued)).status,
            LogSubmitStatus::kQueued);
    }

    auto probe = MakeLoggerAttr(
        logger,
        LogLevel::ERROR,
        "waiting-probe");
    ASSERT_TRUE(probe->seal());

    auto submit_future = std::async(
        std::launch::async,
        [&dispatcher, probe = std::move(probe)]() mutable {
            return dispatcher.submit(std::move(probe));
        });

    // 给提交线程进入条件变量等待的机会，之后再释放 worker。
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    appender->release();

    ASSERT_EQ(
        submit_future.wait_for(std::chrono::milliseconds(500)),
        std::future_status::ready);
    const auto submit = submit_future.get();
    EXPECT_EQ(submit.status, LogSubmitStatus::kQueued);

    const bool processed = appender->waitForCount(
        1 + static_cast<int>(dispatcher.capacity()) + 1,
        std::chrono::milliseconds(1000));
    EXPECT_TRUE(dispatcher.shutdown());
    EXPECT_TRUE(processed);
}

/*
测试思路：记录槽位仍有余量，但总字节预算被第二条大记录占满。第三条较小的
ERROR 记录必须等待字节预算释放；worker 弹出第二条后，not_full_cv_ 被通知，第三条
应成功入队，验证条件变量等待谓词覆盖记录数和字节数两个维度。

预算图：max_bytes=10；8B -> queued_bytes=8；4B -> 等待；释放 8B -> 4B queued。
*/
TEST(TestLog, AsyncDispatcherWakesWaitingProducerAfterByteBudgetReleased)
{
    LogAsyncConfig config;
    config.queue_capacity = 4;
    config.max_queue_bytes = 10;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_byte_wakeup");
    auto appender = std::make_shared<BlockingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    auto first = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "a");
    ASSERT_TRUE(first->seal());
    ASSERT_EQ(
        dispatcher.submit(std::move(first)).status,
        LogSubmitStatus::kQueued);
    ASSERT_TRUE(appender->waitUntilEntered(
        std::chrono::milliseconds(1000)));

    auto second = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "12345678");
    ASSERT_TRUE(second->seal());
    ASSERT_EQ(second->getContent().size(), 8U);
    ASSERT_EQ(
        dispatcher.submit(std::move(second)).status,
        LogSubmitStatus::kQueued);

    auto probe = MakeLoggerAttr(
        logger,
        LogLevel::ERROR,
        "1234");
    ASSERT_TRUE(probe->seal());

    auto submit_future = std::async(
        std::launch::async,
        [&dispatcher, probe = std::move(probe)]() mutable {
            return dispatcher.submit(std::move(probe));
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    appender->release();

    ASSERT_EQ(
        submit_future.wait_for(std::chrono::milliseconds(500)),
        std::future_status::ready);
    const auto submit = submit_future.get();
    const bool processed = appender->waitForCount(
        3,
        std::chrono::milliseconds(200));
    EXPECT_TRUE(dispatcher.shutdown());

    EXPECT_EQ(submit.status, LogSubmitStatus::kQueued);
    EXPECT_TRUE(processed);
}

/*
测试思路：在同一个满载队列上同时放置多个 ERROR 生产者。worker 每消费一条记录
就释放一个容量并 notify_one，生产者应逐个被唤醒，最终所有记录都能入队并完成，
验证条件变量通知不会只唤醒一次后停滞。

并发图：3 producers -> not_full_cv_；worker pop/notify_one -> 逐个入队 -> appender。
示例：first、second 加 3 条 probe 共 5 条，最终 Counting 结果为 5。
*/
TEST(TestLog, AsyncDispatcherProgressesMultipleWaitingProducers)
{
    constexpr int kProducers = 3;

    LogAsyncConfig config;
    // 当前 MPMC 队列契约要求容量至少为 2。
    config.queue_capacity = 2;
    config.max_queue_bytes = 4096;
    config.stop_drain_timeout_ms = 1000;

    LogAsyncDispatcher dispatcher(config);
    dispatcher.start();

    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_async_multiple_waiters");
    auto appender = std::make_shared<BlockingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    appender->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    auto first = MakeLoggerAttr(
        logger,
        LogLevel::INFO,
        "blocking-first");
    ASSERT_TRUE(first->seal());
    ASSERT_EQ(
        dispatcher.submit(std::move(first)).status,
        LogSubmitStatus::kQueued);
    ASSERT_TRUE(appender->waitUntilEntered(
        std::chrono::milliseconds(1000)));

    for(size_t index = 0; index < dispatcher.capacity(); ++index)
    {
        auto queued = MakeLoggerAttr(
            logger,
            LogLevel::INFO,
            "queued-" + std::to_string(index));
        ASSERT_TRUE(queued->seal());
        ASSERT_EQ(
            dispatcher.submit(std::move(queued)).status,
            LogSubmitStatus::kQueued);
    }

    std::vector<std::future<LogSubmitResult>> submissions;
    submissions.reserve(kProducers);
    for(int index = 0; index < kProducers; ++index)
    {
        auto probe = MakeLoggerAttr(
            logger,
            LogLevel::ERROR,
            "waiting-probe-" + std::to_string(index));
        ASSERT_TRUE(probe->seal());

        submissions.emplace_back(std::async(
            std::launch::async,
            [&dispatcher, probe = std::move(probe)]() mutable {
                return dispatcher.submit(std::move(probe));
            }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    appender->release();

    for(auto& submission : submissions)
    {
        ASSERT_EQ(
            submission.wait_for(std::chrono::milliseconds(1000)),
            std::future_status::ready);
        EXPECT_EQ(submission.get().status, LogSubmitStatus::kQueued);
    }

    ASSERT_TRUE(appender->waitForCount(
        1 + static_cast<int>(dispatcher.capacity()) + kProducers,
        std::chrono::milliseconds(1000)));
    EXPECT_TRUE(dispatcher.shutdown());
}

/*
测试思路：通过真实 applyConfig() 驱动私有路由状态。目标 Logger 先配置为空
Appender 列表进入 Muted；下一次配置移除该专属 Logger，使同一 Logger 对象切回
RootFallback，并继承 root=WARN 的级别。用例结束自动恢复默认配置。

状态图：Own/新建 -> apply(empty appenders) -> Muted
                   -> remove named config -> RootFallback(root=WARN)

示例：Muted 时 FATAL=false；回退 root 后 INFO=false、WARN=true。
*/
TEST(TestLog, ApplyConfigTransitionsMutedLoggerBackToRootFallback)
{
    ScopedDefaultLogConfig restore_default;
    auto& manager = LogManager::GetInstance();
    constexpr char kLoggerName[] = "test_config_route";

    LoggerConfig root_config;
    root_config.name = "root";
    root_config.level = LogLevel::WARN;
    root_config.appenders.push_back(LogAppenderConfig{
        .type = LogAppenderType::kStdout,
        .level = LogLevel::WARN,
    });

    LoggerConfig muted_config;
    muted_config.name = kLoggerName;
    muted_config.level = LogLevel::DEBUG;

    LogConfig muted_log_config;
    muted_log_config.loggers = {root_config, muted_config};
    manager.applyConfig(muted_log_config);
    auto logger = manager.getLogger(kLoggerName);
    ASSERT_NE(logger, nullptr);
    EXPECT_FALSE(logger->shouldLog(LogLevel::FATAL));

    LogConfig root_only_config;
    root_only_config.loggers = {root_config};
    manager.applyConfig(root_only_config);
    EXPECT_EQ(manager.getLogger(kLoggerName), logger);
    EXPECT_FALSE(logger->shouldLog(LogLevel::INFO));
    EXPECT_TRUE(logger->shouldLog(LogLevel::WARN));
}

/*
测试思路：BlockingAppender 在 append 内暂停当前派发。Logger 已在锁内复制
shared_ptr 快照并释放 appenders_mtx_，因此另一个线程应能在 Appender 尚未返回时
完成 delAppender()；快照强引用保证在途派发仍安全完成。

时序图：
  log thread:    snapshot -> append(blocked) ----------------> complete
  remove thread:                  delAppender -> complete

示例：删除在 1 秒内完成，释放阻塞后 append count=1，Logger 进入 Muted。
*/
TEST(TestLog, AppenderSnapshotAllowsRemovalDuringInFlightDispatch)
{
    auto logger = std::make_shared<Logger>(
        &LogManager::GetInstance(), "test_snapshot_dispatch");
    auto appender = std::make_shared<BlockingAppender>();
    logger->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);

    std::thread log_thread([logger] {
        logger->log(MakeLoggerAttr(
            logger,
            LogLevel::INFO,
            "in-flight"));
    });

    const bool entered = appender->waitUntilEntered(
        std::chrono::milliseconds(1000));

    std::promise<void> removed_promise;
    auto removed = removed_promise.get_future();
    std::thread remove_thread([
        logger,
        appender,
        &removed_promise] {
        logger->delAppender(appender);
        removed_promise.set_value();
    });

    const bool removed_while_blocked =
        removed.wait_for(std::chrono::milliseconds(1000))
        == std::future_status::ready;

    appender->release();
    log_thread.join();
    remove_thread.join();

    EXPECT_TRUE(entered);
    EXPECT_TRUE(removed_while_blocked);
    EXPECT_EQ(appender->count(), 1);
    EXPECT_FALSE(logger->shouldLog(LogLevel::FATAL));
}

/*
测试思路：先写入包含换行、CR、TAB、NUL 和 ESC 的正文，再 seal，验证正文保持
原样且进入不可变状态；随后再次写入必须被拒绝。控制字符的可见转义由 Appender
规范化测试覆盖。

状态图：mutable -> seal -> sealed -> getSS/format 抛 logic_error。
示例：正文仍包含真实 `\n`，第二次 seal 返回 false。
*/
TEST(TestLog, LogAttrSealFreezesControlCharactersAndRejectsMutation)
{
    std::string content("line\nnext\r\t", 11);
    content.push_back('\0');
    content.push_back('\x1b');
    auto attr = MakeLogAttr(content);

    ASSERT_TRUE(attr->seal());
    EXPECT_TRUE(attr->isSealed());
    EXPECT_NE(attr->getContent().find('\n'), std::string::npos);
    EXPECT_NE(attr->getContent().find('\r'), std::string::npos);
    EXPECT_NE(attr->getContent().find('\t'), std::string::npos);
    EXPECT_NE(attr->getContent().find('\0'), std::string::npos);
    EXPECT_NE(attr->getContent().find('\x1b'), std::string::npos);
    EXPECT_FALSE(attr->seal());
    EXPECT_THROW(attr->getSS(), std::logic_error);
    EXPECT_THROW(attr->format("later"), std::logic_error);
}

/*
测试思路：将 Appender 的最终记录上限设为 64 字节，输入 200 字节正文，验证限制
发生在 formatter 之后，并且输出仍然只有一个终止换行。

路径图：LogAttr -> formatter -> NormalizeAndLimitRecord -> FileAppender -> sink。
示例：200 个 `x` 被截断为不超过 64 字节，并包含实际后缀
`...[truncated 200 bytes]`。
*/
TEST(TestLog, AppenderNormalizesAndTruncatesFinalFormattedRecord)
{
    TempLogFile file("record_limit");
    LogFileSinkRegister registry;
    auto sink = registry.acquire(file.path());
    ASSERT_NE(sink, nullptr);

    FileAppender appender(sink);
    appender.setFormatter("%m");
    appender.setMaxRecordBytes(64);
    AppendAttr(appender, MakeLogAttr(std::string(200, 'x')));
    ASSERT_TRUE(sink->flush().ok());

    const auto output = ReadFile(file.path());
    ASSERT_LE(output.size(), 64U);
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
    EXPECT_NE(output.find("...[truncated 200 bytes]"), std::string::npos);
    EXPECT_EQ(output.find('\n'), output.size() - 1);
}

TEST(TestLog, AppenderEscapesControlsAndPreservesUtf8)
{
    TempLogFile file("control_utf8");
    LogFileSinkRegister registry;
    auto sink = registry.acquire(file.path());
    ASSERT_NE(sink, nullptr);

    FileAppender appender(sink);
    appender.setFormatter("%m");

    const std::string content =
        "line\nnext-\xE4\xB8\xAD\xE6\x96\x87";
    AppendAttr(appender, MakeLogAttr(content));
    ASSERT_TRUE(sink->flush().ok());

    EXPECT_EQ(
        ReadFile(file.path()),
        "line\\nnext-\xE4\xB8\xAD\xE6\x96\x87\n");
}

/*
测试思路：先累计 8 字节触发字节阈值 flush，再写 ERROR 验证 level flush，最后显式
调用 durableFlush 覆盖 fdatasync 链路。

状态图：0 --4B--> no flush --4B--> threshold flush --ERROR--> level flush。
示例：`info` 不刷新，追加 `more` 后 flush_attempted=true。
*/
TEST(TestLog, SinkFlushesOnLevelAndSupportsDurableFlush)
{
    TempLogFile file("flush_level");
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.flush_threshold = 8;
    config.flush_interval_ms = 30000;
    registry.setConfig(config);

    auto sink = registry.acquire(file.path());
    ASSERT_NE(sink, nullptr);

    auto info = sink->append("info", LogLevel::INFO);
    EXPECT_TRUE(info.ok());
    EXPECT_FALSE(info.flush_attempted);

    auto threshold = sink->append("more", LogLevel::INFO);
    EXPECT_TRUE(threshold.ok());
    EXPECT_TRUE(threshold.flush_attempted);

    auto error = sink->append("error", LogLevel::ERROR);
    EXPECT_TRUE(error.ok());
    EXPECT_TRUE(error.flush_attempted);

    EXPECT_TRUE(sink->durableFlush().ok());
    EXPECT_EQ(ReadFile(file.path()), "infomoreerror");
}

/*
测试思路：active 达到阈值时先保持不动，下一条记录使预计大小越界后先轮转再写；
连续轮转后归档数量必须受配置上限约束。

路径图：active(10B) -> append(1B) -> archive#0 + active(1B) -> repeated rotate。
示例：首次归档内容是 `1234567890`，active 内容是 `x`，最终归档不超过 2 个。
*/
TEST(TestLog, SinkRotatesBeforeNextOversizedWriteAndCleansArchives)
{
    TempLogDirectory directory("rotation");
    const auto active = directory.path() / "net.log";
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.rotate_max_bytes = 10;
    config.rotate_max_backup_files = 2;
    config.flush_threshold = 10 * 1024 * 1024;
    config.flush_interval_ms = 30000;
    registry.setConfig(config);

    auto sink = registry.acquire(active.string());
    ASSERT_NE(sink, nullptr);
    ASSERT_TRUE(sink->append("1234567890", LogLevel::INFO).ok());
    EXPECT_EQ(ReadFile(active.string()), "1234567890");

    ASSERT_TRUE(sink->append("x", LogLevel::INFO).ok());
    EXPECT_EQ(ReadFile(active.string()), "x");

    size_t archive_count = 0;
    for(const auto& entry : std::filesystem::directory_iterator(directory.path()))
    {
        if(entry.path().filename().string().find("net_") == 0)
        {
            ++archive_count;
            EXPECT_EQ(ReadFile(entry.path().string()), "1234567890");
        }
    }
    EXPECT_EQ(archive_count, 1U);

    for(int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(sink->append("1234567890", LogLevel::INFO).ok());
    }

    archive_count = 0;
    for(const auto& entry : std::filesystem::directory_iterator(directory.path()))
    {
        if(entry.path().filename().string().find("net_") == 0)
        {
            ++archive_count;
        }
    }
    EXPECT_LE(archive_count, 2U);
}

/*
测试思路：预先创建一个已经超过阈值的 active，acquire/open 只恢复大小，不做启动
轮转；下一次 append 才执行同步轮转。

状态图：open active(11B) -> no archive -> append(1B) -> rotate -> active(1B)。
示例：open 后仍读到 `01234567890`，append 后 active 只包含 `x`。
*/
TEST(TestLog, SinkDoesNotRotateOversizedActiveFileDuringOpen)
{
    TempLogDirectory directory("rotation_startup");
    const auto active = directory.path() / "net.log";
    {
        std::ofstream output(active, std::ios::binary);
        output << "01234567890";
    }

    LogFileSinkRegister registry;
    LogFileConfig config;
    config.rotate_max_bytes = 10;
    config.rotate_max_backup_files = 2;
    config.flush_threshold = 10 * 1024 * 1024;
    config.flush_interval_ms = 30000;
    registry.setConfig(config);

    auto sink = registry.acquire(active.string());
    ASSERT_NE(sink, nullptr);
    EXPECT_EQ(sink->currentFileSize(), 11U);
    EXPECT_EQ(ReadFile(active.string()), "01234567890");

    size_t archives_before_append = 0;
    for(const auto& entry : std::filesystem::directory_iterator(directory.path()))
    {
        if(entry.path().filename().string().find("net_") == 0)
        {
            ++archives_before_append;
        }
    }
    EXPECT_EQ(archives_before_append, 0U);

    ASSERT_TRUE(sink->append("x", LogLevel::INFO).ok());
    EXPECT_EQ(ReadFile(active.string()), "x");
}

/*
测试思路：通过真实配置创建一个文件 sink，再从 LogManager 外层依次调用全局和单
路径 flush、durable flush、reopen，验证 manager 正确委托 registry。

路径图：LogManager -> LogFileSinkRegister snapshot/find -> LogFileSink -> backend。
示例：批量操作均成功，healthSnapshot 中能定位到规范化后的文件路径。
*/
TEST(TestLog, LogManagerDelegatesBatchFileOperations)
{
    TempLogFile file("manager_batch");
    auto& manager = LogManager::GetInstance();

    LogConfig config;
    LoggerConfig root;
    root.name = "root";
    root.level = LogLevel::DEBUG;
    root.appenders.push_back(LogAppenderConfig{
        .type = LogAppenderType::kFile,
        .level = LogLevel::DEBUG,
        .file_path = file.path(),
    });
    config.loggers.push_back(root);
    manager.applyConfig(config);

    EXPECT_TRUE(manager.flushAll().ok());
    EXPECT_TRUE(manager.durableFlushAll().ok());
    EXPECT_TRUE(manager.reopenAll().ok());
    EXPECT_TRUE(manager.flush(file.path()).ok());
    EXPECT_TRUE(manager.durableFlush(file.path()).ok());

    const auto health = manager.healthSnapshot();
    ASSERT_FALSE(health.sinks.empty());
    EXPECT_EQ(health.sinks.front().path,
        std::filesystem::weakly_canonical(file.path()).string());

    manager.applyConfig(MakeManagerTestConfig());
}

/*
测试思路：在全部提交测试完成后关闭全局 manager，验证 Running 到 Stopped 的状态
迁移，并覆盖重复 shutdown 的幂等语义。

状态图：Running -> StopAccepting -> Stopped -> shutdown -> Stopped。
示例：两次 shutdown 都返回成功，最终状态保持 kStopped。
*/
TEST(TestLog, LogManagerShutdownIsIdempotent)
{
    auto& manager = LogManager::GetInstance();
    ASSERT_EQ(manager.state(), LogManagerState::kRunning);

    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_EQ(manager.state(), LogManagerState::kStopped);
    EXPECT_TRUE(manager.shutdown().ok());
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    auto& manager = LogManager::GetInstance();
    const auto initialize_result = manager.initialize(
        MakeManagerTestConfig());
    if(!initialize_result.ok())
    {
        std::cerr << "log manager test initialization failed: "
                  << initialize_result.message << '\n';
        return EXIT_FAILURE;
    }

    const int result = RUN_ALL_TESTS();
    if(manager.state() != LogManagerState::kStopped)
    {
        (void)manager.shutdown();
    }
    return result;
}
