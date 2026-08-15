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
#include <future>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <optional>
#include <pthread.h>
#include <sstream>
#include <string>
#include <thread>
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

LogAttr::Ptr MakeLogAttr(const std::string &content, uint64_t timestamp = 0)
{
    auto logger = std::make_shared<Logger>("test_log");
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
    void log(LogAttr::Ptr attr) override
    {
        if(attr && attr->getLevel() >= getLevel())
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
    void log(LogAttr::Ptr) override
    {
        std::unique_lock<std::mutex> lock(gate_mtx_);
        entered_ = true;
        entered_cv_.notify_all();
        release_cv_.wait(lock, [this] { return released_; });
        count_.fetch_add(1, std::memory_order_relaxed);
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
    std::mutex gate_mtx_;
    std::condition_variable entered_cv_;
    std::condition_variable release_cv_;
    bool entered_{false};
    bool released_{false};
    std::atomic_int count_{0};
};

class ScopedDefaultLogConfig
{
public:
    ~ScopedDefaultLogConfig()
    {
        try
        {
            LogManager::GetInstance().applyConfig(DefaultLogConfig());
        }
        catch(...)
        {
        }
    }
};

static void ClearTestLogFile()
{
    ASSERT_GE(system("rm -f /tmp/*_test.log"), 0);
}

} // namespace

/*
测试思路：使用默认阈值写入一条小日志，第一次写入不应立即 flush，析构时
才保证缓冲区落盘。

示例：append("first") -> 文件仍为空 -> appender 析构 -> 文件为 "first"。
*/
TEST(TestLog, FileAppenderDefaultWriteMaxSizeDoesNotFlushSmallFirstWrite)
{
    TempLogFile file("default_threshold");

    {
        FileAppender appender(file.path());
        appender.setFormatter("%m");

        appender.append(MakeLogAttr("first"));

        ASSERT_EQ(ReadFile(file.path()), "");
    }

    ASSERT_EQ(ReadFile(file.path()), "first");

    ClearTestLogFile();
}

/*
测试思路：配置 8 字节阈值，验证阈值按累计格式化结果计算，而不是按单次
append 的长度计算。

示例："abc" + "defgh" 达到 8 字节并 flush，之后的 "z" 留在缓冲区。
*/
TEST(TestLog, FileAppenderFlushesAfterCumulativeConfiguredBytes)
{
    TempLogFile file("cumulative_threshold");

    {
        FileAppender appender(file.path());
        appender.setFormatter("%m");
        appender.setFlushThreshold(8);

        appender.append(MakeLogAttr("abc"));
        ASSERT_EQ(ReadFile(file.path()), "");

        appender.append(MakeLogAttr("defgh"));
        ASSERT_EQ(ReadFile(file.path()), "abcdefgh");

        appender.append(MakeLogAttr("z"));
        ASSERT_EQ(ReadFile(file.path()), "abcdefgh");
    }

    ASSERT_EQ(ReadFile(file.path()), "abcdefghz");

    ClearTestLogFile();
}

/*
测试思路：配置 0 字节阈值，验证每次 append 都立即可读，覆盖 flush 边界。

示例：连续 append("a"), append("b") 后文件内容应立即为 "ab"。
*/
TEST(TestLog, FileAppenderZeroWriteMaxSizeFlushesEveryWrite)
{
    TempLogFile file("zero_threshold");

    FileAppender appender(file.path());
    appender.setFormatter("%m");
    appender.setFlushThreshold(0);

    appender.append(MakeLogAttr("a"));
    ASSERT_EQ(ReadFile(file.path()), "a");

    appender.append(MakeLogAttr("b"));
    ASSERT_EQ(ReadFile(file.path()), "ab");

    ClearTestLogFile();
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
    auto logger = std::make_shared<Logger>("test_own_appender");
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
    auto logger = std::make_shared<Logger>("test_macro_dispatch");
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

    manager.applyConfig(LogConfig{{root_config, muted_config}});
    auto logger = manager.getLogger(kLoggerName);
    ASSERT_NE(logger, nullptr);
    EXPECT_FALSE(logger->shouldLog(LogLevel::FATAL));

    manager.applyConfig(LogConfig{{root_config}});
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
    auto logger = std::make_shared<Logger>("test_snapshot_dispatch");
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

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
