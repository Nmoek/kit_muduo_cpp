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
    return _pattern == ctx->request()->path();
}


GlobRouterMatcher::GlobRouterMatcher(const std::string& pattern)
    :RouterMatcher(pattern)
{

}

bool GlobRouterMatcher::Match(HttpContextPtr ctx)
{
    HTTP_F_DEBUG("GlobRouterMatcher::Match %s %s \n", _pattern.c_str(), ctx->request()->path().c_str());
    
    return ::fnmatch(_pattern.c_str(), ctx->request()->path().c_str(), 0) == 0;
}


RegexRouterMatcher::RegexRouterMatcher(const std::string& pattern, const std::string& replace)
    :RouterMatcher(pattern)
    ,_replacePattern(replace)
    ,_replaceRegex(":[^/]+")
    ,_paramRegex(":([^/]+)")
{
    _targetPattern = "^";
    _targetPattern += std::regex_replace(pattern, _replaceRegex, _replacePattern);
    _targetPattern += "$";


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
        // 对照原始模版 提取动态参数 
        auto param_it = std::sregex_iterator(_pattern.begin(), _pattern.end(), _paramRegex);
        auto match_it = ++matches.begin(); // 跳过完整匹配

        while(param_it != std::sregex_iterator() && match_it != matches.end())
        {
            req->addQureyParam((*param_it)[1].str(), match_it->str());
            HTTP_F_DEBUG("regex match: %s : %s \n", (*param_it)[1].str().c_str(), match_it->str().c_str());
            
            ++param_it;
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

}
}
 