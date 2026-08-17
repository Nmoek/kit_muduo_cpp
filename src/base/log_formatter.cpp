/**
 * @file log_formatter.cpp
 * @brief 日志输出格式器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 21:39:48
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log_formatter.h"

#include <cctype>
#include <iostream>
#include <sstream>

namespace kit_muduo {

namespace {

thread_local std::stringstream t_formatter_ss;


/**
 * @brief 从预处理过的模版map中反向匹配当前模版此时位置上的 字符串 是否合法
 * @param cur_pattern 
 * @param begin 
 * @return LogFormatter::ItemMap::const_iterator 
 */
LogFormatter::ItemMap::const_iterator FindLongestItem(const std::string &cur_pattern, size_t begin_pos)
{
    const auto& item_map = LogFormatter::GetMap();
    auto selected = item_map.end();
    size_t selected_size = 0;


    for(auto it = item_map.begin();it != item_map.end();++it)
    {
        const std::string& main_pattern = it->first;
        if(main_pattern.size() <= selected_size
            || begin_pos + main_pattern.size() > cur_pattern.size()
            || cur_pattern.compare(begin_pos, main_pattern.size(), main_pattern) != 0)
        {
            continue;
        }
        selected = it;
        selected_size = main_pattern.size();
    }

    return selected;
}

inline bool HasSubPattern(const std::string& main_pattern)
{
    return "d" == main_pattern;
}


} //namespace


/*
    %n-------换行符 '\n'
    %m-------日志内容
    %le-------level日志级别
    %r-------程序启动到现在的耗时
    %%-------输出一个'%'
    %tid-----用户进程线程TID
    %pid-----内核线程PID
    %T-------Tab键
    %tn------当前线程名称
    %d-------日期和时间
    %f-------文件名
    %l-------行号
    %gn-------日志器名字
    %mn------模块名字

一般情况：%n [%l] <%T> ....
特殊情况1:  %d{%Y-%M-%d %H:%m:%s.%ms}  {...}表示子模版
特殊情况2: 12345%%tn  正确解析: 12345 + %% + tn  以左先匹配为准
特殊情况3：%tnabc%n  正确解析: %tn + abc + %n  错误解析: %t + nabc + %n

*/

// 预处理模版对应的子类
const LogFormatter::ItemMap& LogFormatter::GetMap()
{
    static const ItemMap m = {
#define XX(P, ITEM) \
        {#P, [](const std::string &sub_pattern){ return std::make_shared<ITEM>(sub_pattern); }}

        XX(n, NewLineFormatItem),
        XX(m, ContentFormatItem),
        XX(le, LevelFormatItem),
        XX(r, ElapseFormatItem),
        XX(tid, ThreadTidFormatItem),
        XX(pid, ThreadPidFormatItem),
        XX(T, TabFormatItem),
        XX(tn, ThreadNameFormatItem),
        XX(d, DateTimeFormatItem),
        XX(f, FileFormatItem),
        XX(l, LineFormatItem),
        XX(gn, LogNameFormatItem),
        XX(mn, ModuleNameFormatItem),
#undef XX
        };
    return m;
}


FormatItem::FormatItem(const std::string& sub_pattern)
    :sub_pattern_(sub_pattern)
{

}


LogFormatter::LogFormatter(const std::string& pattern)
    :pattern_(pattern)
{
    init();
}

// std::regex LogFormatter::_patternReg(R"((%%) | (%([a-zA-Z]\{(.*?)\})) | (%([a-zA-Z]+)) | ([^%]+))");


#if 1
// 手写有限状态机版本 
// HACK放弃正则匹配的原因是: 1. 存在匹配漏洞 静默忽略 2.错误原因难以表达 3. 手写性能和正则匹配差不多
void LogFormatter::init()
{
    valid_ = true;
    error_.clear();
    format_items_.clear();

    std::string text;
    std::string main_pattern;

    for(size_t i = 0;i < pattern_.size();)
    {
        // 1. 普通字符串缓存处理
        if(pattern_[i] != '%')
        {
            const size_t text_be_pos = i;
            while(i < pattern_.size() && pattern_[i] != '%')
            {
                ++i;
            }
            text = pattern_.substr(text_be_pos, i - text_be_pos);
            // 普通字符串加入缓存 空串不存
            if(!text.empty())
            {
                format_items_.push_back(std::make_shared<StringFormatItem>(text));
            }

            continue;
        }

        // 2. 已经碰到 %
        text.clear();
        ++i; // 跳过 '%'
        if(i >= pattern_.size())
        {
            fail("formatter ends with an incomplete '%' item");
            return;
        }

        // 单独判断 %% 的转义情况
        if('%' == pattern_[i])
        {
            format_items_.push_back(std::make_shared<StringFormatItem>("%"));
            ++i;
            continue;
        }


        main_pattern.clear();

        // 找到最长的合法模版串  %tn --> %t or %tn 只记录tn
        // 否则将 %xxx 当成一个普通字符串处理
        const auto it = FindLongestItem(pattern_, i);
        if(GetMap().end() == it || !it->second)
        {
            fail("unknown formatter item: '%"
                + pattern_.substr(i) + "'");
            return;
        }
        main_pattern = it->first;
        i += main_pattern.size();

        //3. 处理{...} 子模版情况
        std::string sub_pattern;
        if(i < pattern_.size() && '{' == pattern_[i])
        {
            if(!HasSubPattern(main_pattern))
            {
                fail("formatter item '%" + main_pattern
                    + "' does not accept a sub-pattern");
                return;
            }

            const size_t sub_begin_pos = i + 1;
            auto pos = pattern_.find('}', sub_begin_pos);
            if(std::string::npos == pos)
            {
                fail("formatter item subpattern not found '}': " + pattern_.substr(i));
                return;
            }
            if(sub_begin_pos == pos)
            {
                fail("formatter item subpattern empty: " + pattern_.substr(i));
                return;
            }
            sub_pattern = pattern_.substr(sub_begin_pos, pos - sub_begin_pos);
            i = pos + 1;
        }

        format_items_.push_back(it->second(sub_pattern));
    }
}
#else
// 正则表达式版本
// 使用到 regex的 字符串分割 技巧
void LogFormatter::init()
{
    if(pattern_.empty())
    {
        return;
    }

    std::regex _patternReg(R"((%%)|(%([a-zA-Z]\{(.*?)\}))|(%([a-zA-Z]+))|([^%]+))");

    // 正则处理
    std::sregex_iterator result_it(pattern_.begin(), pattern_.end(), _patternReg);
    std::sregex_iterator end_it;

    while(result_it != end_it)
    {
        const std::smatch &match = *result_it;
/*
    这里的下标表示的含义：
         0  1     2     3         4        5    6              7
        (%  %) | (%( [a-zA-Z] \{(.*?)\} )|(%([a-zA-Z]+))) | ([^%]+))
*/
        if (match[1].matched) // 处理转义%%
        { 
            // 直接构造 字符串对象
            format_items_.push_back(std::make_shared<StringFormatItem>("%"));
        }
        else if (match[2].matched) // %__{ __ }处理带子格式的变量
        { 
            auto it = GetMap().find(match[3]);
            if(it != GetMap().end())
            {
                format_items_.push_back(it->second(match[4]));
            }
        }
        else if (match[5].matched) // %__ 处理普通变量
        { 
            auto it = GetMap().find(match[6]);
            if(it != GetMap().end())
            {
                format_items_.push_back(it->second(""));
            }
        }
        else if (match[7].matched) // 处理普通文本(前缀不带%)
        { 
            std::string text = match[7];
            if (!text.empty())
            {
                format_items_.push_back(std::make_shared<StringFormatItem>(text));
            }
        }

        ++result_it;
    }

}
#endif


std::string LogFormatter::format(const LogAttr::Ptr& pattr)
{
    t_formatter_ss.str("");
    t_formatter_ss.clear();
    
    for(auto &fi : format_items_)
    {
        if(fi)
        {
            fi->format(t_formatter_ss, pattr);
        }
    }

    return t_formatter_ss.str();
}
} // namespace kit