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


namespace kit_muduo::http {

static const char kSpace[] = " ";
static const char kCRLF[] = "\r\n";
static const char kColon[] = ":";

HttpRequest::HttpRequest()
{
    HTTP_F_DEBUG("HttpRequest::construct() %p\n", this);
}
HttpRequest::~HttpRequest()
{
    HTTP_F_DEBUG("HttpRequest::~HttpRequest() %p\n", this);
}

void HttpRequest::addHeader(const std::string& head, const std::string &val)
{
    headers_[head] = val;
}

bool HttpRequest::addHeader(const char *start, const char *colon, const char *end)
{
    assert(start != end);
    std::string head(start, colon);
    DelSpaceHelper(head);
    // assert(head.size() != 0);
    if(head.size() <= 0)
    {
        return false;
    }
    ++colon;
    std::string val(colon, end);
    DelSpaceHelper(val);
    // assert(val.size() != 0);
    HTTP_F_DEBUG("Header: |%s|-|%s|\n", head.c_str(), val.c_str());
    if(val.size() <= 0)
    {
        return false;
    }
    headers_[head] = val;
    return true;
}

std::string HttpRequest::getHeader(const std::string &key) const
{
    if(headers_.empty())
        return "";
    auto it = headers_.find(key);
    return it == headers_.end() ? "" : it->second;
}

std::string HttpRequest::toString()
{
    std::stringstream ss{""};
    // Line
    ss << method_.toString();
    ss << kSpace;
    ss << path_;
    ss << kSpace;
    ss << version_.toString();
    ss << kCRLF;

    if(body_.data().size())
        headers_["Content-Length"] = std::to_string(body_.data().size());

    // Headers
    for(auto &it : headers_)
    {
        ss << it.first;
        ss << kColon << kSpace;
        ss << it.second;
        ss << kCRLF;
    }
    ss << kCRLF;
    ss << body_.toString();
    return ss.str();
}


}
