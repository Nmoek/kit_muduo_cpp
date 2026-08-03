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
namespace kit_muduo
{

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

void LogAppender::append(LogAttr::Ptr pattr)
{
    std::unique_lock<std::mutex> lock(mtx_);
    log(pattr);
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

void ConsoleAppender::log(LogAttr::Ptr pattr)
{

    if(pattr->getLevel() < level_)
        return;

    if(formatter_)
        std::cout << formatter_->format(pattr);
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
        file_.close();
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

} //namespace kit