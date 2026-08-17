/**
 * @file log_appender.cpp
 * @brief 日志输出器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 20:06:40
 * @copyright Copyright (c) 2025 Kewin Li
 */


#include <iostream>
#include <stdexcept>
#include <string.h>
#include <errno.h>

#include "base/log_appender.h"
#include "base/log_file_sink.h"
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
}

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

#if MUDUO_LOG_APPENDER_OPTIMIZE
void LogAppender::append(LogAttr::Ptr attr)
{
    
    if(!attr || attr->getLevel() < level_.load(std::memory_order_acquire))
    {
        return;
    }
    std::unique_lock<std::mutex> lock(mtx_);
    auto formatter = formatter_;
    lock.unlock();

    if(!formatter)
    {
        return;
    }

    const std::string &log_data = formatter->format(attr);
    if(log_data.empty())
    {
        return;
    }

    log(log_data);
}

#else 
void LogAppender::append(LogAttr::Ptr attr)
{
    std::unique_lock<std::mutex> lock(mtx_);
    log(attr);
}
#endif

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

void ConsoleAppender::log(LogAttr::Ptr attr)
{
    if(!attr || attr->getLevel() < level_)
        return;

    if(formatter_)
    {
        std::cout << formatter_->format(attr);
    }
}


void ConsoleAppender::log(const std::string& log_data)
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
    return file_sink_->ensureOpen(error_message);
}


void FileAppender::log(LogAttr::Ptr attr)
{
    if(!attr || attr->getLevel() < level_)
        return;
    

    if(formatter_)
    {
        file_sink_->append(formatter_->format(attr));
    }
}


void FileAppender::log(const std::string& log_data)
{
    // TODO 返回值暂时没用
   (void)file_sink_->append(log_data);
}

} //namespace kit
