/**
 * @file log_level.h
 * @brief 日志级别
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 19:26:14
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __LOG_LEVEL_H__
#define __LOG_LEVEL_H__

#include <string>

namespace kit_muduo {

/**
 * @brief 日志级别
 */
class LogLevel
{
public:
    enum Level{
        UNKNOW = 0,
        DEBUG    ,
        INFO     ,
        WARN     ,
        ERROR    ,
        FATAL    ,
    };

    static inline bool IsValid(LogLevel::Level level)
    {
        return level >= DEBUG && level <= FATAL;
    }

    /**
     * @brief 枚举---->字符串
     * @param[in] level  日志级别
     * @return std::string
     */
    static std::string ToString(Level level);

    static LogLevel::Level FromString(const std::string &value);
};



} // namespace kit
#endif