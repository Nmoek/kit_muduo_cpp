/**
 * @file log_formatter.cpp
 * @brief 日志输出格式器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 21:39:48
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log_formatter.h"

#include <iostream>

namespace kit_muduo
{

/****************FormatItem******************/



/*
    %n-------换行符 '\n'
    %m-------日志内容
    %p-------level日志级别
    %r-------程序启动到现在的耗时
    %%-------输出一个'%'
    %t-------当前线程ID
    %T-------Tab键
    %tn------当前线程名称
    %d-------日期和时间
    %f-------文件名
    %l-------行号
    %g-------日志器名字
    %mo------模块名字
    %c-------当前协程ID

一般情况：%n [%l] <%T> ....
特殊情况1:  %d{%Y-%M-%d %H:%m:%s.%ms}  {...}表示子模版
特殊情况2: 12345%%tn  正确解析: 12345 + %% + tn  以左先匹配为准
特殊情况3：%tnabc%n  正确解析: %tn + abc + %n  错误解析: %t + nabc + %n

*/

// 预处理模版对应的子类
// BUG【FIX】: 预处理有问题
LogFormatter::ItemMap& LogFormatter::GetMap()
{
    static ItemMap m = {
        #define XX(P, ITEM) \
            {#P, [](const std::string &sub_pattern){ return std::make_shared<ITEM>(sub_pattern); }}

            XX(n, NewLineFormatItem),
            XX(m, ContentFormatItem),
            XX(p, LevelFormatItem),
            XX(r, ElapseFormatItem),
            XX(t, ThreadIdFormatItem),
            XX(T, TabFormatItem),
            XX(tn, ThreadNameFormatItem),
            XX(d, DateTimeFormatItem),
            XX(f, FileFormatItem),
            XX(l, LineFormatItem),
            XX(g, LogNameFormatItem),
            XX(mo, ModuleNameFormatItem),
        #undef XX
        };
    return m;
}


FormatItem::FormatItem(const std::string& sub_pattern)
{

}




/****************LogFormatter******************/

LogFormatter::LogFormatter(const std::string& pattern)
    :_pattern(pattern)
{
    init();
}

// std::regex LogFormatter::_patternReg(R"((%%) | (%([a-zA-Z]\{(.*?)\})) | (%([a-zA-Z]+)) | ([^%]+))");

/**
 * @brief  判断当前字符是否是合法的模版字符范围(a~z A~Z) 不支持数字以及其他符号
 * @param[in] c
 * @return true
 * @return false
 */
static inline bool isPatternChar(char c)
{
    return (c >= 'a' &&  c <= 'z') || (c >= 'A' && c <= 'Z');
}

#if 0
// 有限状态机版本
void LogFormatter::init()
{
    if(_pattern.empty())
        return;

    // 主模版标识符 + 子模版串 + 主模版是否有效
    using PatternTmp = std::tuple<std::string, std::string, bool>;
    std::vector<PatternTmp> tmpv;

    std::string tmpStr{""};
    std::string patternStr{""};
    std::string subPatternStr{""};
    bool isVaild = false;

    for(int i = 0;i < _pattern.size();)
    {
        // 1. 普通字符串缓存处理
        if(_pattern[i] != '%')
        {
            tmpStr += _pattern[i];
            ++i;
            continue;
        }

        // 普通字符串加入缓存
        if(!tmpStr.empty())
            tmpv.push_back({tmpStr, "", false});

        // 2. 已经碰到 %
        tmpStr.clear();

        ++i; // 跳过 '%'
        patternStr = "";

        // 只记录最长的合法模版串  %tn --> %t or %tn 只记录tn
        // 否则将 %xxx 当成一个普通字符串处理
        while(i < _pattern.size() && isPatternChar(_pattern[i]))
        {
            tmpStr += _pattern[i];

            // 出现第一个不合法模版就退出
            if(FormatItem::_s_pattern2Item.find(tmpStr) != FormatItem::_s_pattern2Item.end() || "%" == tmpStr)
            {
                patternStr = tmpStr.size() > patternStr.size() ? tmpStr : patternStr;
            }
            else
            {
                break;
            }
            ++i;
        }
        if(i >= _pattern.size())
            break;

        //3. 处理{...} 子模版情况
        isVaild = false;
        if(!patternStr.empty())  // 存在主模版
        {
            subPatternStr = "";
            if('{' == _pattern[i])
            {
                int npos = _pattern.find('}', i);
                if(npos >= 0)  //说明没有合法{...}
                {
                    subPatternStr = _pattern.substr(i + 1, npos - i - 1);
                    i = npos + 1;
                }
            }
            isVaild = true;
        }
        else
        {
            patternStr += "%";
            isVaild = false;
        }

        tmpv.push_back({patternStr, subPatternStr, isVaild});
        tmpStr.clear();
    }
    if(!tmpStr.empty())
        tmpv.push_back({tmpStr, "", false});

    // 构建模版对象
    for(auto &v : tmpv)
    {
        std::cout << "'"<< std::get<0>(v) << "'" << ", "
            << "'"<< std::get<1>(v) << "'" << ", "
            << std::get<2>(v) << std::endl;
        if(!std::get<2>(v))
        {
            // 创建普通字符串构建对象
            _formatItems.push_back(nullptr);
        }
        else
        {
            auto it = FormatItem::_s_pattern2Item.find(std::get<0>(v));
            if(it == FormatItem::_s_pattern2Item.end())
            {
                std::cerr << "parse format pattern error! "
                    << "'" << std::get<0>(v) << "'" << std::endl;
            }
            else
            {
                // 根据模版参数构建指定的对象类型
                _formatItems.push_back(nullptr);
            }
        }


    }
}
#else
// 正则表达式版本
// 使用到 regex的 字符串分割 技巧
void LogFormatter::init()
{
    if(_pattern.empty())
        return;

    std::regex _patternReg(R"((%%)|(%([a-zA-Z]\{(.*?)\}))|(%([a-zA-Z]+))|([^%]+))");

    // 正则处理
    std::sregex_iterator result_it(_pattern.begin(), _pattern.end(), _patternReg);
    std::sregex_iterator end_it;
    std::smatch match;


    while(result_it != end_it)
    {
        match = *result_it;

        /*
            这里的下标表示的含义：
              0  1     2    3          4        5     6            7
             (%  %) | (%( [a-zA-Z] \{(.*?)\} )|(%([a-zA-Z]+))) | ([^%]+))
        */
        if (match[1].matched) { // 处理转义%%
            // 直接构造 字符串对象
            _formatItems.push_back(std::make_shared<StringFormatItem>("%"));
        }
        else if (match[2].matched) { // 处理带格式的变量
            std::cout << match[3] << " ";
            std::cout << match[4] << " ";
            auto it = GetMap().find(match[3]);
            if(it != GetMap().end())
            {
                _formatItems.push_back(it->second(match[4]));
            }
        }
        else if (match[5].matched) { // 处理普通变量
            auto it = GetMap().find(match[6]);
            if(it != GetMap().end())
            {
                _formatItems.push_back(it->second(""));
            }
        }
        else if (match[7].matched) { // 处理普通文本
            std::string text = match[7];
            if (!text.empty())
            {
                _formatItems.push_back(std::make_shared<StringFormatItem>(text));
            }
        }

        ++result_it;
    }

}
#endif



std::string LogFormatter::format(LogAttr::Ptr pattr)
{
    std::stringstream ss;

    for(auto &i : _formatItems)
    {
        if(i)
            i->format(ss, pattr);
    }

    return ss.str();
}
} // namespace kit