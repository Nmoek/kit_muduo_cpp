/**
 * @file test_time_stamp.cpp
 * @brief 时间戳、时间格式化和时钟来源测试
 */
#include "base/time_stamp.h"
#include "base/util.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace kit_muduo;

namespace {

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

constexpr int64_t kEpochMilliseconds = 1750856400123LL;

} // namespace

/*
测试总链路：

  固定 Unix 毫秒
        |
        +--> UTC RFC3339: 2025-06-25T13:00:00.123Z
        |
        +--> 固定 UTC+08:00 日志: 2025-06-25 21:00:00.123
        |
        +--> RFC3339 offset 解析 --> 原始 Unix 毫秒

测试目标是锁定“同一个瞬时有两种展示方式”，而不是验证当前主机时区。
*/

/*
测试思路：给定计划中的固定 epoch，分别验证 API UTC 输出和日志 UTC+08:00
输出，确保毫秒始终是三位且不会重复追加。

示例：1750856400123 ->
  API  2025-06-25T13:00:00.123Z
  日志 2025-06-25 21:00:00.123
*/
TEST(TestTimeStamp, FixedEpochFormatsAsUtcAndFixedOffsetLogTime)
{
    const TimeStamp stamp(kEpochMilliseconds);

    EXPECT_EQ(stamp.toUtcRfc3339(), "2025-06-25T13:00:00.123Z");
    EXPECT_EQ(stamp.toLogString(), "2025-06-25 21:00:00.123");
    EXPECT_EQ(
        stamp.toLogString("%Y/%m/%d %H:%M:%S"),
        "2025/06/25 21:00:00.123");

    const TimeStamp stamp_with_seconds(kEpochMilliseconds + 65 * 1000);
    EXPECT_EQ(
        stamp_with_seconds.toUtcRfc3339(),
        "2025-06-25T13:01:05.123Z");
    EXPECT_EQ(
        stamp_with_seconds.toLogString(),
        "2025-06-25 21:01:05.123");
}

/*
测试思路：覆盖 Unix epoch 零点和 epoch 前一毫秒，验证负值仍按“秒主体 +
非负三位毫秒余数”格式化。

示例：-1ms -> 1969-12-31T23:59:59.999Z，日志时间为 1970-01-01 07:59:59.999。
*/
TEST(TestTimeStamp, FormatsEpochBoundariesWithMillisecondPrecision)
{
    EXPECT_EQ(TimeStamp(0).toUtcRfc3339(), "1970-01-01T00:00:00.000Z");
    EXPECT_EQ(TimeStamp(0).toLogString(), "1970-01-01 08:00:00.000");

    EXPECT_EQ(TimeStamp(-1).toUtcRfc3339(), "1969-12-31T23:59:59.999Z");
    EXPECT_EQ(TimeStamp(-1).toLogString(), "1970-01-01 07:59:59.999");
}

/*
测试思路：用同一个瞬时构造无小数、毫秒小数、Z、正 offset 和负 offset
输入，所有合法表示都必须解析回同一个 epoch 毫秒。

示例：
  2025-06-25T21:00:00.123+08:00
  2025-06-25T14:30:00.123+01:30
  2025-06-25T05:00:00.123-08:00
  三者都应得到 1750856400123。
*/
TEST(TestTimeStamp, ParsesValidRfc3339FormsToTheSameInstant)
{
    const std::vector<std::string> values{
        "2025-06-25T13:00:00Z",
        "2025-06-25T13:00:00.123Z",
        "2025-06-25T21:00:00.123+08:00",
        "2025-06-25T14:30:00.123+01:30",
        "2025-06-25T05:00:00.123-08:00",
    };

    for(const auto &value : values)
    {
        SCOPED_TRACE(value);
        const auto parsed = TimeStamp::ParseRfc3339(value);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->millSeconds(), value == values.front()
            ? kEpochMilliseconds - 123
            : kEpochMilliseconds);
    }
}

/*
测试思路：将日期字段、offset、毫秒精度和尾随字符分别置为非法，验证解析
失败返回 nullopt，不能像 mktime 那样静默修正输入。

示例：2025-02-29 不存在、+24:00 越界、.1234 超过毫秒精度、Ztrailing 有尾随字符。
*/
TEST(TestTimeStamp, RejectsInvalidRfc3339Input)
{
    const std::vector<std::string> values{
        "2025-02-29T13:00:00Z",
        "2025-06-25T13:00:00+24:00",
        "2025-06-25T13:00:00+08:60",
        "2025-06-25T13:00:00.1234Z",
        "2025-06-25T13:00:00.123Ztrailing",
        "2025-06-25 13:00:00.123Z",
        "2025-06-25T13:00:60.123Z",
    };

    for(const auto &value : values)
    {
        SCOPED_TRACE(value);
        EXPECT_FALSE(TimeStamp::ParseRfc3339(value).has_value());
    }
}

/*
测试思路：在 UTC、Asia/Shanghai 和一个西半球时区之间切换进程 TZ，再对同一个
epoch 调用 API 和日志格式化。预期值不变，证明实现不依赖 localtime_r。

示例：TZ=UTC 与 TZ=America/Los_Angeles 下，日志都必须是 21:00:00.123。
*/
TEST(TestTimeStamp, FormattingIgnoresProcessTimezone)
{
    const TimeStamp stamp(kEpochMilliseconds);
    const std::vector<const char *> timezones{
        "UTC",
        "Asia/Shanghai",
        "America/Los_Angeles",
    };

    for(const char *timezone : timezones)
    {
        ScopedTimezone scoped_timezone(timezone);
        SCOPED_TRACE(timezone);
        EXPECT_EQ(stamp.toUtcRfc3339(), "2025-06-25T13:00:00.123Z");
        EXPECT_EQ(stamp.toLogString(), "2025-06-25 21:00:00.123");
    }
}

/*
测试思路：连续读取墙上毫秒、墙上微秒和单调时钟，验证 NowUs 与 NowMs 使用
同一 CLOCK_REALTIME 语义，单调值不会倒退。

示例：NowUs()/1000 应落在两次 NowMs() 采样区间内；MonotonicNowMs 的后一次
采样应大于等于前一次采样。
*/
TEST(TestTimeStamp, ClockApisUseExpectedUnitsAndDirections)
{
    const int64_t wall_before_ms = TimeStamp::NowMs();
    const int64_t wall_sample_us = TimeStamp::NowUs();
    const int64_t wall_after_ms = TimeStamp::NowMs();
    const int64_t wall_sample_ms = wall_sample_us / 1000;

    EXPECT_GE(wall_sample_ms, wall_before_ms - 1);
    EXPECT_LE(wall_sample_ms, wall_after_ms + 1);

    const int64_t monotonic_before_ms = TimeStamp::MonotonicNowMs();
    const int64_t monotonic_after_ms = TimeStamp::MonotonicNowMs();
    EXPECT_LE(monotonic_before_ms, monotonic_after_ms);

    const int64_t monotonic_seconds = TimeStamp::MonotonicNowS();
    EXPECT_GE(monotonic_seconds * 1000, monotonic_after_ms - 1000);
    EXPECT_LE(monotonic_seconds * 1000, monotonic_after_ms + 1000);
}

/*
测试思路：验证遗留 Timer2Str 入口只做兼容委托，输出必须与 TimeStamp 的固定
UTC+08:00 日志格式一致，同时保留自定义秒级 pattern。

示例：1750856400 秒 -> 2025-06-25 21:00:00.000。
*/
TEST(TestTimeStamp, Timer2StrDelegatesToFixedLogFormatting)
{
    EXPECT_EQ(
        TimeStamp::FormatLogTimeStamp(1750856400000, "%Y-%m-%d %H:%M:%S"),
        "2025-06-25 21:00:00.000");
    EXPECT_EQ(
        TimeStamp::FormatLogTimeStamp(
            kEpochMilliseconds, "%Y/%m/%d %H:%M:%S"),
        "2025/06/25 21:00:00.123");
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
