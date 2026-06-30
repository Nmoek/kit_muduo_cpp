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
    SetOrReplaceHeader(headers_, head, val);
    if(IsHeaderName(head, "Content-Type"))
    {
        content_meta_ = ParseHttpContentType(val);
    }
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
    addHeader(head, val);
    return true;
}

std::string HttpRequest::getHeader(const std::string &key) const
{
    return GetHeaderIgnoreCase(headers_, key);
}

void HttpRequest::setHeaders(const std::unordered_map<std::string, std::string> &headers)
{
    headers_ = headers;
    content_meta_ = ParseHttpContentType(getHeader("Content-Type"));
}

void HttpRequest::setContentMeta(const ContentMeta& meta)
{
    setContentMeta(ContentMeta(meta));
}

void HttpRequest::setContentMeta(ContentMeta&& meta)
{
    content_meta_ = std::move(meta);
    const std::string content_type = ToContentTypeHeaderValue(content_meta_);
    if(content_type.empty())
    {
        EraseHeader(headers_, "Content-Type");
    }
    else
    {
        SetOrReplaceHeader(headers_, "Content-Type", content_type);
    }
}

void HttpRequest::setBodyData(const std::vector<char>& data)
{
    body_data_.assign(data.begin(), data.end());
}

void HttpRequest::setBodyData(const std::string& data)
{
    body_data_.assign(data.begin(), data.end());
}

void HttpRequest::appendBodyData(const char* start, size_t len)
{
    if(start == nullptr || len == 0)
    {
        return;
    }
    body_data_.insert(body_data_.end(),
        reinterpret_cast<const uint8_t*>(start),
        reinterpret_cast<const uint8_t*>(start + len));
}

void HttpRequest::appendBodyData(const std::string& data)
{
    appendBodyData(data.data(), data.size());
}

void HttpRequest::appendBodyData(const std::vector<char>& data)
{
    body_data_.insert(body_data_.end(), data.begin(), data.end());
}

void HttpRequest::appendBodyData(const std::vector<uint8_t>& data)
{
    body_data_.insert(body_data_.end(), data.begin(), data.end());
}

std::string HttpRequest::bodyString() const
{
    return std::string(body_data_.begin(), body_data_.end());
}

std::vector<uint8_t> HttpRequest::toBytes()
{
    std::stringstream ss{""};
    // Line
    ss << method_.toStr();
    ss << kSpace;
    ss << path_;
    ss << kSpace;
    ss << version_.toStr();
    ss << kCRLF;

    auto headers = headers_;

    if(!body_data_.empty())
    {
        SetOrReplaceHeader(headers, "Content-Length", std::to_string(body_data_.size()));
    }

    const std::string content_type = ToContentTypeHeaderValue(content_meta_);
    if(!content_type.empty())
    {
        SetOrReplaceHeader(headers, "Content-Type", content_type);
    }

    // Headers
    for(const auto &it : headers)
    {
        ss << it.first;
        ss << kColon << kSpace;
        ss << it.second;
        ss << kCRLF;
    }
    ss << kCRLF;
    const std::string header = ss.str();

    std::vector<uint8_t> out;
    out.reserve(header.size() + body_data_.size());
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), body_data_.begin(), body_data_.end());
    return out;
}

std::string HttpRequest::toString()
{
    const auto bytes = toBytes();
    return std::string(bytes.begin(), bytes.end());
}


}
