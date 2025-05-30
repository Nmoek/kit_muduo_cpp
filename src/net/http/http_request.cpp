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

namespace kit_muduo {
namespace http {

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
    _headers[head] = val;
}

std::string HttpRequet::getHeader(const std::string &key) const
{
    auto it = _headers.find(key);
    return it == _headers.end() ? "" : it->second;
}


}
}