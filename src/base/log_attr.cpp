/**
 * @file log_attr.cpp
 * @brief 日志器属性
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-18 23:08:20
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log_attr.h"
#include "base/log_inner.h"

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <cstring>

namespace kit_muduo {


LogAttr::LogAttr(std::shared_ptr<Logger> logger, LogLevel::Level level, const std::string &loggerName, const std::string &module, const char* fileName, int32_t line, uint32_t elapse, pthread_t tid, pid_t pid, const char* threadName, uint64_t timeStamp)
    :time_stamp_(timeStamp)
    ,elapse_(elapse)
    ,level_(level)
    ,line_(line)
    ,tid_(tid)
    ,thread_name_(threadName)
    ,pid_(pid)
    ,file_name_(fileName)
    ,logger_(logger)
    ,logger_name_(loggerName)
    ,module_(module)
{

}

std::string LogAttr::getFileBaseName() const
{
    const size_t pos = file_name_.find_last_of("/\\");
    if(pos == std::string::npos)
    {
        return file_name_;
    }
    return file_name_.substr(pos + 1);
}

std::stringstream& LogAttr::getSS() 
{
    if(sealed_)
    {
        throw std::logic_error("log attribute sealed");
    }
    return content_; 
}


void LogAttr::format(const char *fmt, ...)
{
    if (sealed_)
    {
        throw std::logic_error("log attribute  sealed");
    }

    va_list va;
    va_start(va, fmt);
    format(fmt, va);
    va_end(va);
}

void LogAttr::format(const char *fmt, va_list va)
{
    if (sealed_)
    {
        throw std::logic_error("log attribute  sealed");
    }

    char *buf = nullptr;
    int len = vasprintf(&buf, fmt, std::move(va));
    if(-1 != len)
    {
        try {
            content_.write(buf, len);
            free(buf);
        } catch(const std::exception &e) {
            LOG_INNER_EXCPTION("log attr format exception: %s\n", e.what());
        } catch(...) {
            LOG_INNER_EXCPTION("log attr format unknown exception\n");
        }

    }
}

bool LogAttr::seal()
{
    if(sealed_)
    {
        return false;
    }

    content_.clear();
    sealed_ = true;
    return true;
}


} // namespace kit_mduuo
