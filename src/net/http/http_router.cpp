/**
 * @file http_router.cpp
 * @brief HTTP路由路径(url)处理
 * @author ljk5
 * @version 1.0
 * @date 2025-08-14 16:11:25
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "net/http/http_router.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/net_log.h"

#include <fnmatch.h>


namespace kit_muduo
{
namespace http
{

RouterMatcher::RouterMatcher(const std::string& pattern)
    :_pattern(pattern)
{

}


ExactRouterMatcher::ExactRouterMatcher(const std::string& pattern)
    :RouterMatcher(pattern)
{

}

bool ExactRouterMatcher::Match(HttpContextPtr ctx)
{
    return MatchPath(ctx->request()->path());
}

bool ExactRouterMatcher::MatchPath(const std::string &path) const
{
    return _pattern == path;
}


GlobRouterMatcher::GlobRouterMatcher(const std::string& pattern)
    :RouterMatcher(pattern)
{

}

bool GlobRouterMatcher::Match(HttpContextPtr ctx)
{
    HTTP_F_DEBUG("GlobRouterMatcher::Match %s %s \n", _pattern.c_str(), ctx->request()->path().c_str());
    
    return MatchPath(ctx->request()->path());
}

bool GlobRouterMatcher::MatchPath(const std::string &path) const
{
    return ::fnmatch(_pattern.c_str(), path.c_str(), FNM_PATHNAME) == 0;
}


RegexRouterMatcher::RegexRouterMatcher(const std::string& pattern, const std::string& replace)
    :RouterMatcher(pattern)
    ,_replacePattern(replace)
    ,_replaceRegex(":[^/]+")
    ,_paramRegex(":([^/]+)")
{
    _targetPattern = BuildTargetPattern(pattern, &_paramNames);


    std::regex r{_targetPattern};
    _targetRegex.swap(r);

    HTTP_DEBUG() << _pattern << " --> " << _targetPattern << std::endl;


}

bool RegexRouterMatcher::Match(HttpContextPtr ctx)
{
    std::smatch matches;
    auto req = ctx->request();
    const std::string url = req->path();



    if (std::regex_match(url, matches, _targetRegex)) 
    {
        auto match_it = ++matches.begin(); // 跳过完整匹配

        for(const auto &param_name : _paramNames)
        {
            if(match_it == matches.end())
            {
                break;
            }

            req->addRouteParam(param_name, match_it->str());
            HTTP_F_DEBUG("regex match: %s : %s \n", param_name.c_str(), match_it->str().c_str());
            ++match_it;
        }
        HTTP_F_DEBUG("regex match success! %s \n", url.c_str());
    }
    else
    {
        HTTP_F_WARN("regex_match error!  ori_pattern[%s], url[%s]\n", _pattern.c_str(), url.c_str());
        return false;
    }

    return true;
}

bool RegexRouterMatcher::MatchPath(const std::string &path) const
{
    return std::regex_match(path, _targetRegex);
}

std::string RegexRouterMatcher::BuildTargetPattern(const std::string &pattern, std::vector<std::string> *param_names)
{
    std::string target = "^";
    for(size_t i = 0; i < pattern.size();)
    {
        if(pattern[i] == ':')
        {
            const size_t name_start = i + 1;
            size_t name_end = name_start;
            while(name_end < pattern.size() && pattern[name_end] != '/')
            {
                ++name_end;
            }

            if(name_end > name_start)
            {
                if(param_names)
                {
                    param_names->emplace_back(pattern.substr(name_start, name_end - name_start));
                }
                target += "([^/]+)";
                i = name_end;
                continue;
            }
        }

        if(IsRegexSpecialChar(pattern[i]))
        {
            target.push_back('\\');
        }
        target.push_back(pattern[i]);
        ++i;
    }
    target += "$";
    return target;
}

bool RegexRouterMatcher::IsRegexSpecialChar(char ch)
{
    switch(ch)
    {
        case '\\':
        case '^':
        case '$':
        case '.':
        case '|':
        case '?':
        case '*':
        case '+':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
            return true;
        default:
            return false;
    }
}

}
}
 
