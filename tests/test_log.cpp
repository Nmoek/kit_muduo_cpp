/**
 * @file test_log.cpp
 * @brief 日志组件专项测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-05
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log.h"

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
        LogLevel::Level level,
        bool,
        size_t) override
    {
        if(level >= getLevel())
        {
            count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    int count() const noexcept
    {
        return count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic_int count_{0};
};

class BlockingAppender final : public LogAppender
{
public:
    void append(const std::string&,
        LogLevel::Level,
        bool,
        size_t) override
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

private:
    void blockUntilReleased()
    {
        std::unique_lock<std::mutex> lock(gate_mtx_);
        entered_ = true;
        entered_cv_.notify_all();
        release_cv_.wait(lock, [this] { return released_; });
        count_.fetch_add(1, std::memory_order_relaxed);
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
    registry.setFileConfig(config);

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
    registry.setFileConfig(config);

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
测试思路：日志宏先执行 shouldLog()，通过后由 LogAttrWrap 析构调用
logUnchecked()。计数必须只增加一次，证明宏路径能够实际派发且没有重复提交。

示例：KIT_DEBUG(logger) << "once" -> CountingAppender count 从 0 变为 1。
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

    EXPECT_EQ(appender->count(), 1);
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
    registry.setFileConfig(config);

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
    registry.setFileConfig(config);

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
    registry.setFileConfig(config);

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
