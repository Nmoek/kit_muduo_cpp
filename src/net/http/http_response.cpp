/**
 * @file http_response.cpp
 * @brief HTTP响应
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 15:31:59
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_response.h"
#include "base/util.h"

#include <sstream>

namespace kit_muduo {
namespace http {

static const char kSpace[] = " ";
static const char kCRLF[] = "\r\n";
static const char kColon[] = ":";

std::unordered_map<int32_t, std::string> HttpResponse::StateCode::s_m_codeMessageMap{
    {k200Ok,                         "OK"},
    {k301MovedPermanently,           "Moved Permanently"},
    {k302MoveTemporarily,            "Move temporarily"},
    {k400BadRequest,                 "Bad Request"},
    {k403Forbidden,                  "Forbidden"},
    {k404NotFound,                   "Not Found"},
    {k500InternalServerError,        "Internal Server Error"},
};

void HttpResponse::addHeader(const std::string& head, const std::string &val)
{
    _headers[head] = val;
}

void HttpResponse::addHeader(const char *start, const char *colon, const char *end)
{
    assert(start != end);
    std::string head(start, colon);
    DelSpaceHelper(head);
    assert(head.size() != 0);
    ++colon;
    std::string val(colon, end);
    DelSpaceHelper(val);
    assert(val.size() != 0);
    _headers[head] = val;
}

std::string HttpResponse::getHeader(const std::string &key) const
{
    auto it = _headers.find(key);
    return it == _headers.end() ? "" : it->second;
}

std::string HttpResponse::toString()
{
    std::stringstream ss{""};
    ss << _version.toString();
    ss << kSpace;
    ss << _stateCode.toString();
    ss << kSpace;
    ss << _stateCode.message();
    ss << kCRLF;
    for(auto &it : _headers)
    {
        ss << it.first;
        ss << kColon << kSpace;
        ss << it.second;
        ss << kCRLF;
    }
    ss << kCRLF;
    ss << _body;
    return ss.str();
}

}
}