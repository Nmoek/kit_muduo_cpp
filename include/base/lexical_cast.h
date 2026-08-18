/**
 * @file lexical_cast.h
 * @brief 自定义实现 万能转换
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-30 15:16:53
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LEXICAL_CAST_H__
#define __KIT_LEXICAL_CAST_H__


#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace kit_muduo {

class BadLexicalCast: public std::runtime_error
{
public:
    BadLexicalCast(std::string from_type, std::string to_type,std::string reason)
        :std::runtime_error("lexical cast" + from_type + "->" + to_type + " failed" + ": " + reason)
        ,from_type_(std::move(from_type))
        ,to_type_(std::move(to_type))
        ,reason_(std::move(reason)) { }
    
    const std::string& fromType() const noexcept { return from_type_; }
    const std::string& toType() const noexcept { return to_type_; }

    const std::string& reason() const noexcept { return reason_; }

private:
    std::string from_type_;
    std::string to_type_;
    std::string reason_;
};

/// @brief 模版常量
template<typename ...>
inline constexpr bool kLexicalCaseUnsupported = false;

/// @brief 严格整数类型校验 不允许字符宽窄类型混入
template<typename T>
inline constexpr bool kLexicalInterger = 
    std::is_integral_v<T>
    && !std::is_same_v<T,bool>
    && !std::is_same_v<T,char>
    && !std::is_same_v<T,signed char>
    && !std::is_same_v<T,unsigned char>
    && !std::is_same_v<T,wchar_t>
    && !std::is_same_v<T,char16_t>
    && !std::is_same_v<T,char32_t>;

/**
 * @brief 类型转换，只完成基础类型转换 type From ---> type To
 * @tparam From 待转换类型
 * @tparam To 转换后类型
 * @tparam Enable 
 */
template<typename From, typename To, typename Enable = void>
class LexicalCast
{
    static_assert(kLexicalCaseUnsupported<From, To>,
        "LexicalCast only supports explicitly declared scalar pairs");
};

// TODO 这里不能使用万能 stream流进行隐式转换
#if 0
{
template<typename From, typename To>
class LexicalCast<From, To>
{
public:
    // From --> To
    To operator()(const From &value) const
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << value;
        if(!out)
        {
            throw BadLexicalCast(typeid(From).name(), typeid(To).name(), "stream output failed");
        }

        std::istringstream in(out.str());
        in.imbue(std::locale::classic());
        To result{};
        in >> std::noskipws >> result;
        if(!in || in.peek() != std::char_traits<char>::eof())
        {
            throw BadLexicalCast(typeid(From).name(), typeid(To).name(), "stream input was not fully consumed");
        }
        return result;
    }
};
}
#endif

/**
 * @brief 特化 特殊处理string <---> string
 * @tparam 
 */
template<>
class LexicalCast<std::string, std::string>
{
public:
    std::string operator()(const std::string &value) const
    {
        return value;
    }
};

/**
 * @brief 特化 特殊处理string ---> bool
 * @tparam  
 */
template<>
class LexicalCast<std::string, bool>
{
public:
    bool operator()(const std::string &value) const
    {
        std::string tmp{value};
        std::for_each(tmp.begin(), tmp.end(), [](auto &c){
            c = std::tolower(c);
        });
        if("true" == tmp || "1" == tmp)
        {
            return true;
        }
        else if("false" == tmp || "0" == tmp)
        {
            return false;
        }
        else
        {
            throw BadLexicalCast("string", "bool", "invalid boolean");
        }
    }
};

/**
 * @brief 特化 特殊处理bool ---> string
 * @tparam  
 */
template<>
class LexicalCast<bool, std::string>
{
public:
    std::string operator()(const bool value) const
    {
        return value ? "true" : "false";
    }
};


/**
 * @brief 偏特化 string --> 可计算数字(整型、浮点型)
 * @tparam T 目标类型
 * @tparam Policy 转换策略
 */
template<typename T>
class LexicalCast<std::string, T, 
    std::enable_if_t<kLexicalInterger<T>>>
{
public:
    T operator()(const std::string &value) const
    {
        if(value.empty())
        {
            throw BadLexicalCast("string", typeid(T).name(), "empty input");
        }

        T result{};
        const char *begin = value.data();
        const char* end = begin + value.size();
        const auto parsed = std::from_chars(begin, end, result, 10);
        if(parsed.ec != std::errc{} || parsed.ptr != end)
        {
            throw BadLexicalCast("string", typeid(T).name(), "invalid or out-of-range integer");
        }

        return result;
    }
};


/**
 * @brief 偏特化 整型数字 ---> string
 * @tparam T 源类型
 * @tparam Policy 转换策略
 */
template<typename T>
class LexicalCast<T, std::string, 
    std::enable_if_t<kLexicalInterger<T>>>
{
public:
    std::string operator()(const T &value) const
    {
        std::array<char,std::numeric_limits<T>::digits10 + 3> result{};
        char *begin = result.data();
        char* end = begin + result.size();
        const auto& parsed = std::to_chars(begin, end, value, 10);
        if(parsed.ec != std::errc{})
        {
            throw BadLexicalCast(typeid(T).name(), "string", "invalid interger value");
        }
        return std::string(begin, parsed.ptr);
    }
};

}
#endif // __KIT_LEXICAL_CAST_H__