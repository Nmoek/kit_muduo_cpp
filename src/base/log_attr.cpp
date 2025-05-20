/**
 * @file log_attr.cpp
 * @brief 日志器属性
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-18 23:08:20
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/log_level.h"
#include "base/log_attr.h"
#include <cstdarg>

#include <iostream>

namespace kit_muduo
{

LogAttr::LogAttr(std::shared_ptr<Logger> logger, LogLevel::Level level, const std::string &loggerName, const std::string &module, const char* fileName, int32_t line, uint32_t elapse, pthread_t tid, pid_t pid, const char* threadName, uint64_t timeStamp)
    :_timeStamp(timeStamp)
    ,_elapse(elapse)
    ,_level(level)
    ,_line(line)
    ,_tid(tid)
    ,_threadName(threadName)
    ,_pid(pid)
    ,_fileName(fileName)
    ,_logger(logger)
    ,_loggerName(loggerName)
    ,_module(module)
{

}

void LogAttr::format(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    format(fmt, va);
    va_end(va);
}

void LogAttr::format(const char *fmt, va_list va)
{
    char *buf = nullptr;
    int len = vasprintf(&buf, fmt, va);
    if(-1 != len)
    {
        _content << buf;
    }
}
} // namespace kit