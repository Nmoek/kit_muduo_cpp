/**
 * @file test_log.cpp
 * @brief 日志组件专项测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-05
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log.h"

#include <cstdlib>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <optional>
#include <pthread.h>
#include <sstream>
#include <string>
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

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
