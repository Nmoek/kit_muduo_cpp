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
#include <pthread.h>
#include <sstream>
#include <string>
#include <unistd.h>

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

std::string ReadFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

LogAttr::Ptr MakeLogAttr(const std::string &content)
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
        0);
    attr->getSS() << content;
    return attr;
}

static void ClearTestLogFile()
{
    ASSERT_GE(system("rm -f /tmp/*_test.log"), 0);
}

} // namespace

TEST(TestLog, FileAppenderDefaultWriteMaxSizeDoesNotFlushSmallFirstWrite)
{
    TempLogFile file("default_threshold");

    {
        FileAppender appender(file.path());
        appender.setFomatter("%m");

        appender.append(MakeLogAttr("first"));

        ASSERT_EQ(ReadFile(file.path()), "");
    }

    ASSERT_EQ(ReadFile(file.path()), "first");

    ClearTestLogFile();
}

TEST(TestLog, FileAppenderFlushesAfterCumulativeConfiguredBytes)
{
    TempLogFile file("cumulative_threshold");

    {
        FileAppender appender(file.path());
        appender.setFomatter("%m");
        appender.setWriteMaxSize(8);

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

TEST(TestLog, FileAppenderZeroWriteMaxSizeFlushesEveryWrite)
{
    TempLogFile file("zero_threshold");

    FileAppender appender(file.path());
    appender.setFomatter("%m");
    appender.setWriteMaxSize(0);

    appender.append(MakeLogAttr("a"));
    ASSERT_EQ(ReadFile(file.path()), "a");

    appender.append(MakeLogAttr("b"));
    ASSERT_EQ(ReadFile(file.path()), "ab");

    ClearTestLogFile();
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
