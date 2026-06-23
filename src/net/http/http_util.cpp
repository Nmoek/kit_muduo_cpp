/**
 * @file http_util.cpp
 * @brief 
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-31 20:20:14
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/http/http_util.h"

#include <algorithm>
#include <cctype>

namespace kit_muduo::http {

bool IsHeaderName(const std::string& actual, const std::string& expected)
{
    return actual.size() == expected.size()
        && std::equal(actual.begin(), actual.end(), expected.begin(),
            [](unsigned char lhs, unsigned char rhs) {
                return std::tolower(lhs) == std::tolower(rhs);
            });
}

std::string GetHeaderIgnoreCase(
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& key)
{
    auto direct_it = headers.find(key);
    if(direct_it != headers.end())
    {
        return direct_it->second;
    }

    for(const auto& item : headers)
    {
        if(IsHeaderName(item.first, key))
        {
            return item.second;
        }
    }

    return "";
}

void SetOrReplaceHeader(std::unordered_map<std::string, std::string>& headers,
                        const std::string& canonical_key,
                        const std::string& value)
{
    for(auto it = headers.begin(); it != headers.end(); ++it)
    {
        if(IsHeaderName(it->first, canonical_key))
        {
            const bool same_key = it->first == canonical_key;
            if(same_key)
            {
                it->second = value;
            }
            else
            {
                headers.erase(it);
                headers[canonical_key] = value;
            }
            return;
        }
    }

    headers[canonical_key] = value;
}

void EraseHeader(std::unordered_map<std::string, std::string>& headers,
                 const std::string& key)
{
    for(auto it = headers.begin(); it != headers.end();)
    {
        if(IsHeaderName(it->first, key))
        {
            it = headers.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::unordered_map<int32_t, std::string> StateCode::s_m_codeMessageMap{
    {kUnknow, ""},
    {k200Ok,                         "OK"},
    {k204NoContent,                  "No Content"},
    {k301MovedPermanently,           "Moved Permanently"},
    {k302MoveTemporarily,            "Move temporarily"},
    {k400BadRequest,                 "Bad Request"},
    {k401Unauthorized,               "Unauthorized"},
    {k403Forbidden,                  "Forbidden"},
    {k404NotFound,                   "Not Found"},
    {k405MethodNotAllowed,           "Method Not Allowed"},
    {k454SessionNotFound, "Session Not Found"},
    {k455MethodNotValid,             "Method Not Valid"},
    {k500InternalServerError,        "Internal Server Error"},
    {k503ServiceUnavailable, "Service Unavailable"}
};


}
