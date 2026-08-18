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

#include <array>
#include <memory>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <functional>
#include <regex>
#include <iostream>

#include "base/log_level.h"
#include "base/log_attr.h"
#include "base/time_stamp.h"


namespace kit_muduo {

constexpr const char* kLogFormatDefaultPattern = "[%d][%le][%f #%l][%gn.%mn]<%pid:%tn> %m";
constexpr const char* kDatetimeFormatDefaultPattern = "%Y-%m-%d %H:%M:%S";

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
    virtual void format(std::stringstream &ss, const LogAttr::Ptr& pattr) = 0;

    /**
     * @brief 判断是否存在子模版
     * @return true 
     * @return false 
     */
    bool hasSub() const noexcept { return !sub_pattern_.empty(); }

protected:
    std::string sub_pattern_;
};

/**
 * @brief %n-----换行符 '\n'
 */
class NewLineFormatItem: public FormatItem
{
public:
    NewLineFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << "\n"; }
};

/**
 * @brief %m-------日志内容
 */
class ContentFormatItem: public FormatItem
{
public:
    ContentFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getContent(); }
};

/**
 * @brief %p-------level 当前日志级别
 */
class LevelFormatItem: public FormatItem
{
public:
    LevelFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << LogLevel::ToString(pattr->getLevel()); }
};

/**
 * @brief %r-------程序启动到现在的耗时
 */
class ElapseFormatItem: public FormatItem
{
public:
    ElapseFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getElapse(); }
};

/**
 * @brief %tid-------用户进程线程TID
 */
class ThreadTidFormatItem: public FormatItem
{
public:
    ThreadTidFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getTid(); }
};

/**
 * @brief %pid-------内核线程PID
 */
class ThreadPidFormatItem: public FormatItem
{
public:
    ThreadPidFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getPid(); }
};

/**
 * @brief %tn------当前线程名称
 */
class ThreadNameFormatItem: public FormatItem
{
public:
    ThreadNameFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getThreadName(); }
};


/**
 * @brief %T-------Tab键
 */
class TabFormatItem: public FormatItem
{
public:
    TabFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << "\t"; }
};

/**
 * @brief %d-------日期和时间
 */
class DateTimeFormatItem: public FormatItem
{
public:
    DateTimeFormatItem(const std::string &str)
        :datetime_format_(str)
    {
        if(datetime_format_.empty())
        {
            datetime_format_ = kDatetimeFormatDefaultPattern;
        }
    }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override
    {
        ss << TimeStamp(pattr->getTimeStamp()).toLogString(datetime_format_);
    }
private:
    /// @brief 时间日期格式化字符串
    std::string datetime_format_{""};
};

/**
 * @brief %f-------文件名
 */
class FileFormatItem: public FormatItem
{
public:
    FileFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getFileBaseName(); }
};

/**
 * @brief %l-------行号
 */
class LineFormatItem: public FormatItem
{
public:
    LineFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getLine(); }
};

/**
 * @brief %g-------日志器名字
 */
class LogNameFormatItem: public FormatItem
{
public:
    LogNameFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << pattr->getLoggerName(); }
};

/**
 * @brief %mo------模块名字
 */
class ModuleNameFormatItem: public FormatItem
{
public:
    ModuleNameFormatItem(const std::string &str = "") { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override
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
        :str_(str)
    { }

    void format(std::stringstream &ss, const LogAttr::Ptr& pattr) override { ss << str_; }
private:
    std::string str_;
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


    explicit LogFormatter(const std::string& pattern = kLogFormatDefaultPattern);

    /**
     * @brief 日志内容格式化
     * @param[in] pattr
     * @return std::string
     */
    std::string format(const LogAttr::Ptr& pattr);

    const std::string &pattern() const noexcept { return pattern_; }

    bool valid() const noexcept { return valid_; }

    const std::string &error() const noexcept { return error_; }

public:
    /**
     * @brief 静态初始化, 调用时加载, 防止启动加载时顺序问题
     * @return ItemMap&
     */
    static const ItemMap& GetMap();
private:
    /**
     * @brief 格式器初始化: 模版解析 格式对象生成
     */
    void init();

    inline void fail(std::string reason)
    {
        valid_ = false;
        error_ = std::move(reason);
        format_items_.clear();
    }

private:
    /// @brief 格式模版字符串
    std::string pattern_{""};
    /// @brief 格式模版子项
    std::vector<FormatItem::Ptr> format_items_;
    /// @brief 当前格式是否合法
    bool valid_{true};
    /// @brief 格式错误原因
    std::string error_;
};



}
#endif
