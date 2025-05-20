/**
 * @file log_attr.h
 * @brief  日志器属性
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 19:28:14
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __LOG_ATTR_H__
#define __LOG_ATTR_H__

#include <memory>
#include <sstream>

namespace kit_muduo {

class Logger;

/**
 * @brief 日志器属性
 */
class LogAttr
{
public:
    using Ptr = std::shared_ptr<LogAttr>;
    using UPtr = std::unique_ptr<LogAttr>;

    /**
     * @brief 构造
     * @param logger
     * @param level
     * @param loggerName
     * @param module
     * @param fileName
     * @param line
     * @param elapse
     * @param tid
     * @param pid
     * @param threadName
     * @param timeStamp
     */
    LogAttr(std::shared_ptr<Logger> logger, LogLevel::Level level, const std::string &loggerName, const std::string &module, const char* fileName, int32_t line, uint32_t elapse, pthread_t tid, pid_t pid, const char* threadName, uint64_t timeStamp);

    uint64_t getTimeStamp() const { return _timeStamp; }

    uint32_t getElapse() const { return _elapse; }

    LogLevel::Level getLevel() const { return _level; }
    int32_t getLine() const { return _line; }

    pthread_t getTid() const { return _tid; }

    std::string getThreadName() const { return _threadName; }

    pid_t getPid() const  { return _pid; }

    std::string getFileName() const { return _fileName; }

    std::string getContent() const { return _content.str(); }


    std::shared_ptr<Logger> getLogger() const { return _logger; };

    std::string getLoggerName() const { return _loggerName; }

    std::string getModule() const { return _module; }

    std::stringstream& getSS() { return _content; }

    /**
     * @brief 实际内容变参模版处理
     * @param[in] fmt  带参模版
     * @param[in] ...  不定参数
     */
    void format(const char *fmt, ...);

    /**
     * @brief 实际内容变参模版处理
     * @param[in] fmt 带参模版
     * @param[in] va 不定参数列表
     */
    void format(const char *fmt, va_list va);

private:
    /// @brief 日志时间戳
    uint64_t _timeStamp{0};
    /// @brief 程序启动到现在的毫秒
    uint32_t _elapse{0};
    /// @brief 当前日志本身的级别
    LogLevel::Level _level;
    /// @brief 日志行号
    int32_t _line{0};
    /// @brief 线程tid
    pthread_t _tid{0};
    /// @brief 线程名称
    std::string _threadName{""};
    /// @brief 线程真实pid
    pid_t _pid{0};
    /// @brief 日志文件路径
    std::string _fileName{""};
    /// @brief 实际日志内容
    std::stringstream _content{""};
    /// @brief 属性属于哪个哪个日志器
    std::shared_ptr<Logger> _logger{nullptr};
    /// @brief 日志器的名称
    std::string _loggerName{""};
    /// @brief 模块名
    std::string _module{""};
};




} //namespace kit
#endif