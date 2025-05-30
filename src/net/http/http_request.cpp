/**
 * @file http_request.cpp
 * @brief HTTP请求
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 21:32:57
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_request.h"
#include "net/net_log.h"
#include "base/util.h"

#include <sstream>

namespace kit_muduo {
namespace http {

static const char kSpace[] = " ";
static const char kCRLF[] = "\r\n";
static const char kColon[] = ":";

void HttpRequet::addHeader(const std::string& head, const std::string &val)
{
    _headers[head] = val;
}

void HttpRequet::addHeader(const char *start, const char *colon, const char *end)
{
    assert(start != end);
    std::string head(start, colon);
    DelSpaceHelper(head);
    assert(head.size() != 0);
    ++colon;
    std::string val(colon, end);
    DelSpaceHelper(val);
    assert(val.size() != 0);
    HTTP_F_DEBUG("Header: |%s|-|%s|\n", head.c_str(), val.c_str());
    _headers[head] = val;
}

std::string HttpRequet::getHeader(const std::string &key) const
{
    auto it = _headers.find(key);
    return it == _headers.end() ? "" : it->second;
}

std::string HttpRequet::toString()
{
    std::stringstream ss{""};
    // Line
    ss << _method.toString();
    ss << kSpace;
    ss << _path;
    ss << kSpace;
    ss << _version.toString();
    ss << kCRLF;

    if(_body.size())
        _headers["Content-Length"] = std::to_string(_body.size());

    // Headers
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