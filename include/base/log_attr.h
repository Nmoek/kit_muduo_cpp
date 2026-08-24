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

#include "base/log_level.h"

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

    uint64_t getTimeStamp() const { return time_stamp_; }
    uint32_t getElapse() const { return elapse_; }
    LogLevel::Level getLevel() const { return level_; }
    int32_t getLine() const { return line_; }
    pthread_t getTid() const { return tid_; }
    const std::string& getThreadName() const { return thread_name_; }
    pid_t getPid() const  { return pid_; }
    const std::string& getFileName() const { return file_name_; }

    /**
     * @brief 获取打印日志位置路径的纯文件名
     * @return std::string
     */
    std::string getFileBaseName() const;

    std::string getContent() const { return content_.str(); }


    std::shared_ptr<Logger> getLogger() const { return logger_; };

    const std::string& getLoggerName() const { return logger_name_; }

    const std::string& getModule() const { return module_; }

    std::stringstream& getSS();

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

    bool seal();
    bool isSealed() const noexcept { return sealed_; }

private:
    /// @brief 日志时间戳
    uint64_t time_stamp_{0};
    /// @brief 程序启动到现在的毫秒
    uint32_t elapse_{0};
    /// @brief 当前日志本身的级别
    LogLevel::Level level_;
    /// @brief 日志行号
    int32_t line_{0};
    /// @brief 线程tid
    pthread_t tid_{0};
    /// @brief 线程名称
    std::string thread_name_{""};
    /// @brief 线程真实pid
    pid_t pid_{0};
    /// @brief 日志文件路径
    std::string file_name_{""};
    /// @brief 实际日志内容
    std::stringstream content_{""};
    /// @brief 属性属于哪个哪个日志器
    std::shared_ptr<Logger> logger_{nullptr};
    /// @brief 日志器的名称
    std::string logger_name_{""};
    /// @brief 模块名
    std::string module_{""};
    /// @brief 不可变状态
    bool sealed_{false};


};




} //namespace kit
#endif
