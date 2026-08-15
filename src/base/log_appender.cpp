/**
 * @file log_appender.cpp
 * @brief 日志输出器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 20:06:40
 * @copyright Copyright (c) 2025 Kewin Li
 */


#include <iostream>
#include <string.h>
#include <errno.h>

#include "base/log_appender.h"
namespace kit_muduo {


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

    if(attr->getLevel() < level_)
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
FileAppender::FileAppender(const std::string &file_name)
    :file_name_(file_name)
    ,cur_size_(0)
    ,flush_threshold_(kDefaultFlushThreshold)
{

}



bool FileAppender::openForAppend(std::string* error_message)
{
    if(file_name_.empty())
    {
        if(error_message)
        {
            *error_message = "file path empty";
        }
        return false;
    }
    if(file_.is_open())
    {
        return true;
    }

    file_.clear();
    file_.open(file_name_, std::ios::out | std::ios::app | std::ios::binary);
    if(!file_.is_open())
    {
        if(error_message)
        {
            *error_message ="cannot open file: " + file_name_
                + ", errno=" + std::to_string(errno)
                + ", message=" + std::strerror(errno);
        }
        return false;
    }

    cur_size_ = 0;
    return true;
}


#if MUDUO_LOG_APPENDER_OPTIMIZE

void FileAppender::log(LogAttr::Ptr attr)
{
    std::unique_lock<std::mutex> lock(mtx_);
    // 保留重打开机制
    if(!file_.is_open() && !openForAppend())
    {
        return;
    }

    if(formatter_)
    {
        const std::string& log_data = formatter_->format(attr);
        cur_size_ += log_data.size();
        file_ << log_data;
        if(cur_size_ >= flush_threshold_)
        {
            file_.flush();
            cur_size_ = 0;
        }
    }
}



#else
void FileAppender::log(LogAttr::Ptr attr)
{
    
    if(!attr || attr->getLevel() < level_)
    {
        return;
    }

    // 保留重打开机制
    if(!file_.is_open() && !openForAppend())
    {
        return;
    }

    if(formatter_)
    {
        const std::string& log_data = formatter_->format(attr);
        cur_size_ += log_data.size();
        file_ << log_data;
        if(cur_size_ >= flush_threshold_)
        {
            file_.flush();
            cur_size_ = 0;
        }
    }
}
#endif 

void FileAppender::log(const std::string& log_data)
{
    std::lock_guard<std::mutex> lock(mtx_);
    // 保留重打开机制
    if(!file_.is_open() && !openForAppend())
    {
        return;
    }

    cur_size_ += log_data.size();
    file_ << log_data;
    if(cur_size_ >= flush_threshold_)
    {
        file_.flush();
        cur_size_ = 0;
    }
    
}

} //namespace kit