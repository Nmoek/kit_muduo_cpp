/**
 * @file log.cpp
 * @brief 日志器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 19:32:10
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log.h"
#include <iostream>

namespace kit_muduo
{

Logger::Logger(const std::string &name)
    :_name(name)
{
    addAppender(std::make_shared<ConsoleAppender>());

}

/*
             |  _level(INFO) <= level都能进行输出
             v
    DEBUG   INFO   WARN  ERROR   FATAL
            INFO
*/
void Logger::log(LogAttr::Ptr pattr)
{
    if(pattr->getLevel() < _level)
        return;

    std::unique_lock<std::mutex> lock(_appendersMtx);
    for(auto &a : _appenders)
    {
        if(a)
            a->append(pattr);
    }

}


void Logger::addAppender(LogAppender::Ptr pappender)
{
    std::unique_lock<std::mutex> lock(_appendersMtx);
    _appenders.push_back(pappender);
}

void Logger::delAppender(LogAppender::Ptr pappender)
{
    std::unique_lock<std::mutex> lock(_appendersMtx);

    for(auto it = _appenders.begin();it != _appenders
    .end();++it)
    {
        if(*it == pappender)
        {
            _appenders.erase(it);
            return;
        }
    }
    return;
}

/**************LogAttrWrap****************/

LogAttrWrap::LogAttrWrap(LogAttr::Ptr attr)
    :_attr(attr)
{ }

LogAttrWrap::~LogAttrWrap()
{
    if(_attr->getLogger()->getLevel() <= _attr->getLevel())
        _attr->getLogger()->log(_attr);
}



/**************LogManager****************/
LogManager::LogManager()
    :_defaultLogger(std::make_shared<Logger>("root"))
{
    _defaultLogger->addAppender(std::make_shared<ConsoleAppender>());
}


Logger::Ptr LogManager::getDefLogger() const
{
    return _defaultLogger;
}

void LogManager::addLogger(const std::string &name, Logger::Ptr logger)
{
    std::unique_lock<std::mutex> lock(_loggersMtx);
    _loggers[name] = logger;
}

Logger::Ptr LogManager::addLogger(const std::string &name)
{
    std::unique_lock<std::mutex> lock(_loggersMtx);
    auto logger = std::make_shared<Logger>(name);
    _loggers[name] = logger;
    return logger;
}

Logger::Ptr LogManager::getLogger(const std::string& name)
{
    std::unique_lock<std::mutex> lock(_loggersMtx);
    auto it = _loggers.find(name);
    if(it != _loggers.end())
        return it->second;
    lock.unlock();
    return addLogger(name);
}

void LogManager::delLogger(const std::string& name)
{
    std::unique_lock<std::mutex> lock(_loggersMtx);
    auto it = _loggers.find(name);
    if(it != _loggers.end())
        _loggers.erase(it);
}


} // namespace kit


