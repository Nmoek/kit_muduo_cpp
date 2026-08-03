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

namespace {
std::string Trim(std::string value)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool EqualIgnoreCase(const std::string &lhs, const std::string &rhs)
{
    return lhs.size() == rhs.size()
        && std::equal(lhs.begin(), lhs.end(), rhs.begin(),
            [](unsigned char a, unsigned char b) {
                return std::tolower(a) == std::tolower(b);
            });
}

inline bool IsHexDigit(char value)
{
    return ('0' <= value && value <= '9')
        || ('a' <= value && value <= 'f')
        || ('A' <= value && value <= 'F');
}

inline uint8_t HexValue(char value)
{
    if('0' <= value && value <= '9')
        return static_cast<uint8_t>(value - '0');

    if('a' <= value && value <= 'f')
        return static_cast<uint8_t>(value - 'a' + 10);

    return static_cast<uint8_t>(value - 'A' + 10);
}

} // namespace

std::unordered_map<int32_t, std::string> StateCode::s_m_codeMessageMap{
    {kUnknow, ""},
    {k100Continue, "Continue"},
    {k101SwitchingProtocols, "Switching Protocols"},
    {k102Processing, "Processing"},
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
    {k413PayloadTooLarge, "Payload Too Large"},
    {k414URITooLong, "URI Too Long"},
    {k431RequestHeaderFieldsTooLarge, "Request Header Fields Too Largeg"},
    {k455MethodNotValid,             "Method Not Valid"},
    {k500InternalServerError,        "Internal Server Error"},
    {k503ServiceUnavailable, "Service Unavailable"}
};

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

bool HeaderContainsToken(const std::string &header_value, const std::string &token)
{
    size_t start = 0;
    while(start < header_value.size())
    {
        size_t pos = header_value.find(",");
        size_t end = (pos == std::string::npos ? header_value.size() : pos);
        if(EqualIgnoreCase(Trim(header_value.substr(0, end - start)), token))
        {
            return  true;
        }
        if(pos == std::string::npos)
        {
            break;
        }
        start = pos + 1;
    }
    return false;
}

std::string NormalizeHttpPath(const std::string &path)
{
    if(path.empty() || path[0] != '/')
    {
        return path;
    }

    std::string normalized;
    normalized.reserve(path.size());

    bool prev_slash = false;
    for(char ch : path)
    {
        if(ch == '/')
        {
            if(prev_slash)
            {
                continue;
            }
            prev_slash = true;
        }
        else
        {
            prev_slash = false;
        }
        normalized.push_back(ch);
    }

    return normalized;
}

std::optional<std::string> PercentDecodeHttpPathOnce(const std::string& raw_path)
{
    std::string decoded;
    decoded.reserve(raw_path.size());

    for(size_t index = 0; index < raw_path.size(); ++index)
    {
        const char current = raw_path[index];

        if(current != '%')
        {
            // URL path 中 '+' 是合法文件名字符，不能按 query 规则转为空格。
            decoded.push_back(current);
            continue;
        }

        if(index + 2 >= raw_path.size()
            || !IsHexDigit(raw_path[index + 1])
            || !IsHexDigit(raw_path[index + 2]))
        {
            return std::nullopt;
        }

        const auto byte = static_cast<char>(
            (HexValue(raw_path[index + 1]) << 4)
            | HexValue(raw_path[index + 2]));

        decoded.push_back(byte);
        index += 2;
    }

    return decoded;
}

}
