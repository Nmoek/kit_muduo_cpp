/**
 * @file http_router.h
 * @brief HTTP路由路径(url)处理
 * @author ljk5
 * @version 1.0
 * @date 2025-08-14 16:05:54
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_HTTP_ROUTER_H__
#define __KIT_HTTP_ROUTER_H__

#include "net/call_backs.h"

#include <string>
#include <memory>
#include <regex>
#include <vector>

namespace kit_muduo
{
namespace http
{

// 命令模式
class RouterMatcher
{
public:
    using Ptr = std::shared_ptr<RouterMatcher>;

    RouterMatcher() = default;

    RouterMatcher(const std::string& pattern);

    virtual ~RouterMatcher() = default;

    void setPattern(const std::string &pattern) { _pattern = pattern; }
    
    std::string pattern() const { return _pattern; }

    virtual bool Match(HttpContextPtr ctx) = 0;
    virtual bool MatchPath(const std::string &path) const = 0;

protected:
    /// @brief 原始匹配的模版 精准 模糊 动态
    std::string _pattern;
};

/**
 * @brief 精准路由匹配
 */
class ExactRouterMatcher: public RouterMatcher
{
public:
    ExactRouterMatcher() = default;

    ExactRouterMatcher(const std::string& pattern);

    bool Match(HttpContextPtr ctx) override;
    bool MatchPath(const std::string &path) const override;
    
};

/**
 * @brief 模糊路由匹配
 */
class GlobRouterMatcher: public RouterMatcher
{
public:

    GlobRouterMatcher() = default;

    GlobRouterMatcher(const std::string& pattern);

    bool Match(HttpContextPtr ctx) override;
    bool MatchPath(const std::string &path) const override;
    
};



/**
 * @brief 路由正则表达式匹配
 */
class RegexRouterMatcher: public RouterMatcher
{
public:
    RegexRouterMatcher() = default;

    RegexRouterMatcher(const std::string& pattern, const std::string& replace = "([^/]+)");

    bool Match(HttpContextPtr ctx) override;
    bool MatchPath(const std::string &path) const override;

private:
    static std::string BuildTargetPattern(const std::string &pattern, std::vector<std::string> *param_names);
    static bool IsRegexSpecialChar(char ch);

    std::string _targetPattern;
    std::string _replacePattern;
    std::vector<std::string> _paramNames;
    /// @brief 目标模版 表达式
    std::regex _targetRegex;
    /// @brief 替换模版 表达式
    std::regex _replaceRegex;
    /// @brief 参数 表达式
    std::regex _paramRegex;

};





} // namespace http
} // namespace kit_muduo

#endif //__KIT_HTTP_ROUTER_H__
