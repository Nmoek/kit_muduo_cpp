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
    :_level(LogLevel::DEBUG)
    ,_formatter(std::make_shared<LogFormatter>())
{

}

LogAppender::LogAppender(LogLevel::Level level, LogFormatter::Ptr formatter)
    :_level(level)
    ,_formatter(formatter)
{

}

void LogAppender::append(LogAttr::Ptr pattr)
{
    std::unique_lock<std::mutex> lock(_formatterMtx);
    log(pattr);
}

void LogAppender::setFomatter(LogFormatter::Ptr pfarmatter)
{
    std::unique_lock<std::mutex> lock(_formatterMtx);
    _formatter = pfarmatter;
}

void LogAppender::setFomatter(const std::string & pattern)
{
    std::unique_lock<std::mutex> lock(_formatterMtx);
    _formatter = std::make_shared<LogFormatter>(pattern);
}


LogFormatter::Ptr LogAppender::getFormatter() const
{
    std::unique_lock<std::mutex> lock(_formatterMtx);
    return _formatter;
}

/*********ConsoleAppender***********/

void ConsoleAppender::log(LogAttr::Ptr pattr)
{

    if(pattr->getLevel() < _level)
        return;

    if(_formatter)
        std::cout << _formatter->format(pattr);
}

/*********FileAppender***********/
FileAppender::FileAppender(const std::string &fileName)
    :_fileName(fileName)
{

}

bool FileAppender::reopen()
{
    if(_f.is_open())
        _f.close();

    _f.open(_fileName, std::ios::app | std::ios::binary);
    if(!_f.is_open())
    {
        std::cerr << _fileName
            << ", open error: "
            << errno << ": " << ::strerror(errno) << std::endl;
        return false;
    }
    return true;
}


void FileAppender::log(LogAttr::Ptr pattr)
{
    if(pattr->getLevel() < _level)
        return;

    if(reopen())
    {
        if(_formatter)
            _f << _formatter->format(pattr);
    }

}
} //namespace kit