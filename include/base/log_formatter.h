/**
 * @file log_formatter.h
 * @brief 日志输出格式器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 18:27:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __LOG_FORMATTER_H__
#define __LOG_FORMATTER_H__

#include <memory>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <functional>
#include <regex>
#include <iostream>

#include "base/log_level.h"
#include "base/log_attr.h"


namespace kit_muduo {

#define LOG_FORMAT_DEFAULT_PATTERN  "[%d][%p][%f #%l][%g.%mo]<%t:%tn> %m"

#define DATETIME_DEFAULT_FORMAT_PATTERN  "%Y-%m-%d %H:%M:%S"


/**
 * @brief 格式项基类
 */
class FormatItem
{
public:
    using Ptr = std::shared_ptr<FormatItem>;

    FormatItem(const std::string& sub_pattern = "");

    virtual ~FormatItem() = default;

    /**
     * @brief 日志内容格式化子项
     * @param[in out] ss  字符串流
     * @param[in] pattr 日志属性
     */
    virtual void format(std::stringstream &ss, LogAttr::Ptr pattr) = 0;

};

/**
 * @brief %n-----换行符 '\n'
 */
class NewLineFormatItem: public FormatItem
{
public:
    NewLineFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << "\n"; }
};

/**
 * @brief %m-------日志内容
 */
class ContentFormatItem: public FormatItem
{
public:
    ContentFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getContent(); }
};

/**
 * @brief %p-------level 当前日志级别
 */
class LevelFormatItem: public FormatItem
{
public:
    LevelFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << LogLevel::ToString(pattr->getLevel()); }
};

/**
 * @brief %r-------程序启动到现在的耗时
 */
class ElapseFormatItem: public FormatItem
{
public:
    ElapseFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getElapse(); }
};

/**
 * @brief %t-------当前线程ID
 */
class ThreadIdFormatItem: public FormatItem
{
public:
    ThreadIdFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getPid(); }
};

/**
 * @brief %tn------当前线程名称
 */
class ThreadNameFormatItem: public FormatItem
{
public:
    ThreadNameFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getThreadName(); }
};


/**
 * @brief %T-------Tab键
 */
class TabFormatItem: public FormatItem
{
public:
    TabFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << "\t"; }
};

/**
 * @brief %d-------日期和时间
 */
class DateTimeFormatItem: public FormatItem
{
public:
    DateTimeFormatItem(const std::string &str)
        :_timeFormat(str)
    {
        if(_timeFormat.empty())
            _timeFormat = DATETIME_DEFAULT_FORMAT_PATTERN;
    }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override
    {
        time_t t = (time_t)(pattr->getTimeStamp() / 1000);
        struct tm tm = {0};
        char timeStr[32] = {0};
        char buf[96] = {0};

        tm = *localtime(&t);
        strftime(timeStr, sizeof(timeStr), _timeFormat.c_str(), &tm);

        snprintf(buf, sizeof(buf), "%s.%03d", timeStr, (int)(pattr->getTimeStamp() % 1000));

        ss << buf;
    }
private:
    /// @brief 时间日期格式化字符串
    std::string _timeFormat{""};
};

/**
 * @brief %f-------文件名
 */
class FileFormatItem: public FormatItem
{
public:
    FileFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getFileName(); }
};

/**
 * @brief %l-------行号
 */
class LineFormatItem: public FormatItem
{
public:
    LineFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getLine(); }
};

/**
 * @brief %g-------日志器名字
 */
class LogNameFormatItem: public FormatItem
{
public:
    LogNameFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << pattr->getLoggerName(); }
};

/**
 * @brief %mo------模块名字
 */
class ModuleNameFormatItem: public FormatItem
{
public:
    ModuleNameFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override
    {
        auto str = pattr->getModule().size() ? pattr->getModule() : "null";
        ss << str;
    }
};

/**
 * @brief 模版中其他普通字符串
 */
class StringFormatItem: public FormatItem
{
public:
    using Ptr = std::shared_ptr<StringFormatItem>;
    StringFormatItem(const std::string &str = "")
        :_str(str)
    { }

    void format(std::stringstream &ss, LogAttr::Ptr pattr) override { ss << _str; }
private:
    std::string _str;
};

/**
 * @brief 日志格式器
 */
class LogFormatter
{
public:
    using Ptr = std::shared_ptr<LogFormatter>;
    // 这里包装一层的作用：延后给构造函数传参,否则得就地传参
    using ItemFuncWrap = std::function<FormatItem::Ptr(const std::string &sub_pattern)>;

    using ItemMap = std::unordered_map<std::string, ItemFuncWrap>;

    LogFormatter(const std::string& pattern = LOG_FORMAT_DEFAULT_PATTERN);

    /**
     * @brief 日志内容格式化
     * @param[in] pattr
     * @return std::string
     */
    std::string format(LogAttr::Ptr pattr);

public:
    /**
     * @brief 静态初始化, 调用时加载, 防止启动加载时顺序问题
     * @return ItemMap&
     */
    static ItemMap& GetMap();
private:
    /**
     * @brief 格式器初始化: 模版解析 格式对象生成
     */
    void init();

private:
    /// @brief 格式模版字符串
    std::string _pattern{""};
    /// @brief 格式模版子项
    std::vector<FormatItem::Ptr> _formatItems;

};



}
#endif