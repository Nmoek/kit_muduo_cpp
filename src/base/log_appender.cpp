/**
 * @file log_appender.cpp
 * @brief 日志输出器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 20:06:40
 * @copyright Copyright (c) 2025 Kewin Li
 */


#include <cstdio>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string.h>
#include <errno.h>
#include <vector>

#include "base/log_appender.h"
#include "base/log_file_sink.h"
#include "base/log_inner.h"
#include "base/util.h"
namespace kit_muduo {

namespace {

inline LogFileSink::Ptr CheckSink(LogFileSink::Ptr sink)
{
    if(!sink)
    {
        throw std::invalid_argument("log file sink null");
    }
    return sink;
}

std::string NormalizeAndLimitRecord(const std::string &formated_data, uint32_t max_record_bytes, bool &truncated)
{
    if(max_record_bytes <= 1)
    {
        throw std::invalid_argument("log record limit invalid: " + std::to_string(max_record_bytes));
    }

    std::string body;
    std::string tmp_data{formated_data};
    if(!tmp_data.empty() && '\n' == tmp_data.back())
    {
        tmp_data.pop_back();
    }
    body.reserve(tmp_data.size());

    // 每个 boundary 都位于一个完整转义片段或完整 UTF-8 字符之后，
    // 截断时只使用这些边界，避免留下半个转义串或半个 UTF-8 字符。
    std::vector<size_t> safe_boundaries;
    safe_boundaries.reserve(tmp_data.size() + 1);
    safe_boundaries.push_back(0);

    for(size_t i = 0; i < tmp_data.size();)
    {
        const auto ch = static_cast<unsigned char>(tmp_data[i]);
        switch (ch)
        {
            case '\n':
                body += "\\n";
                ++i;
                break;
            case '\r':
                body += "\\r";
                ++i;
                break;
            case '\t':
                body += "\\t";
                ++i;
                break;
            case '\0':
                body += "\\0";
                ++i;
                break;
            case 0x1b:
                body += "\\x1b";
                ++i;
                break;
            default:
            {
                if (ch < 0x20)
                {
                    char buffer[8] = {0};
                    snprintf(buffer, sizeof(buffer), "\\x%02x", ch);
                    body += buffer;
                    ++i;
                }
                else
                {
                    size_t sequence_size = 1;
                    if ((ch & 0xE0) == 0xC0)
                    {
                        sequence_size = 2;
                    }
                    else if ((ch & 0xF0) == 0xE0)
                    {
                        sequence_size = 3;
                    }
                    else if ((ch & 0xF8) == 0xF0)
                    {
                        sequence_size = 4;
                    }

                    if (sequence_size > 1
                        && i + sequence_size <= tmp_data.size())
                    {
                        bool valid_continuation = true;
                        for (size_t offset = 1;
                            offset < sequence_size;
                            ++offset)
                        {
                            const auto continuation =
                                static_cast<unsigned char>(
                                    tmp_data[i + offset]);
                            if ((continuation & 0xC0) != 0x80)
                            {
                                valid_continuation = false;
                                break;
                            }
                        }

                        if (valid_continuation)
                        {
                            body.append(tmp_data, i, sequence_size);
                            i += sequence_size;
                            break;
                        }
                    }

                    body.push_back(tmp_data[i]);
                    ++i;
                }
                break;
            }
        }

        safe_boundaries.push_back(body.size());
    }

    if(body.size() > max_record_bytes)
    {
        truncated = true;

        const std::string suffix =
            "...[truncated " + std::to_string(tmp_data.size())
            + " bytes]";

        if(max_record_bytes <= suffix.size() + 1)
        {
            throw std::invalid_argument("log record limit invalid: " + std::to_string(max_record_bytes));
        }

        const size_t prefix_limit =
            max_record_bytes - suffix.size() - 1;
        const auto boundary = std::upper_bound(
            safe_boundaries.begin(),
            safe_boundaries.end(),
            prefix_limit);
        const size_t prefix_size = boundary == safe_boundaries.begin()
            ? 0
            : *std::prev(boundary);

        body.resize(prefix_size);
        body += suffix;
    }

    body += '\n';

    return body;
}


} // namespace

/***********LogAppender************/

LogAppender::LogAppender()
    :level_(LogLevel::DEBUG)
    ,formatter_(std::make_shared<LogFormatter>())
{

}

LogAppender::LogAppender(LogLevel::Level level, LogFormatter::Ptr formatter)
    :level_(level)
    ,formatter_(formatter)
{

}

void LogAppender::append(LogAttr::Ptr attr)
{
    if(!attr)
    {
        return;
    }

    const auto attr_level = attr->getLevel();
    if(getLevel() > attr_level)
    {
        return;
    }

    std::unique_lock<std::mutex> lock(mtx_);
    auto formatter = formatter_;
    if(!formatter)
    {
        return;
    }
    lock.unlock();

    const std::string &formated_data = formatter->format(attr);
    if(formated_data.empty())
    {
        LOG_INNER_WARN("log format error!\n");
        return;
    }

    bool truncated = false;
    const size_t original_bytes = formated_data.size();

    std::string log_data = NormalizeAndLimitRecord(formated_data, max_record_bytes_, truncated);

    append(log_data, attr_level, truncated, original_bytes);
}

void LogAppender::setFormatter(LogFormatter::Ptr pfarmatter)
{
    std::unique_lock<std::mutex> lock(mtx_);
    formatter_ = pfarmatter;
}

void LogAppender::setFormatter(const std::string & pattern)
{
    std::unique_lock<std::mutex> lock(mtx_);
    formatter_ = std::make_shared<LogFormatter>(pattern);
}


LogFormatter::Ptr LogAppender::getFormatter() const
{
    std::unique_lock<std::mutex> lock(mtx_);
    return formatter_;
}


/*********ConsoleAppender***********/

void ConsoleAppender::append(const std::string& log_data,
    LogLevel::Level level,
    bool truncated,
    size_t original_bytes)
{
    std::lock_guard<std::mutex> lock(GetConsoleMtx());
    std::cout << log_data;
}

std::mutex& ConsoleAppender::GetConsoleMtx()
{
    static std::mutex mtx;
    return mtx;
}

/*********FileAppender***********/
FileAppender::FileAppender(LogFileSink::Ptr file_sink)
    :file_sink_(CheckSink(file_sink))
{

}

bool FileAppender::openForAppend(std::string* error_message)
{
    auto result = file_sink_->ensureOpen();
    if(!result.ok())
    {
        if(error_message)
        {
            *error_message = std::move(result.message);
        }
        return false;
    }
    return true;
}


void FileAppender::append(const std::string& log_data,
    LogLevel::Level level,
    bool truncated,
    size_t original_bytes)
{
    auto result = file_sink_->append(log_data, level, truncated, original_bytes);
    if(!result.ok())
    {
        // 使用 cerr通道补充打一次
        std::cerr << "CERR: " << log_data;
    }
}

} //namespace kit
