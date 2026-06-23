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
#include "net/net_log.h"

#include <sstream>
#include <iostream>


namespace kit_muduo::http {

static const char kSpace[] = " ";
static const char kCRLF[] = "\r\n";
static const char kColon[] = ":";


HttpResponse::HttpResponse()
    :state_code_(StateCode::kUnknow)
    ,version_(Version::kUnknow)
    ,connection_closed_(false)
{
    HTTP_DEBUG() << "HttpResponse::construct() " << this << std::endl;
}

HttpResponse::~HttpResponse()
{
    HTTP_DEBUG() << "~HttpResponse " << this <<  std::endl;

}

void HttpResponse::addHeader(const std::string& head, const std::string &val)
{
    SetOrReplaceHeader(headers_, head, val);
    if(IsHeaderName(head, "Content-Type"))
    {
        content_meta_ = ParseHttpContentType(val);
    }
}

bool HttpResponse::addHeader(const char *start, const char *colon, const char *end)
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

std::string HttpResponse::getHeader(const std::string &key) const
{
    return GetHeaderIgnoreCase(headers_, key);
}

void HttpResponse::setHeaders(const std::unordered_map<std::string, std::string> &headers)
{
    headers_ = headers;
    content_meta_ = ParseHttpContentType(getHeader("Content-Type"));
}

void HttpResponse::setContentMeta(const ContentMeta& meta)
{
    setContentMeta(ContentMeta(meta));
}

void HttpResponse::setContentMeta(ContentMeta&& meta)
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

void HttpResponse::setBodyData(const std::vector<char>& data)
{
    body_data_.assign(data.begin(), data.end());
}

void HttpResponse::setBodyData(const std::string& data)
{
    body_data_.assign(data.begin(), data.end());
}

void HttpResponse::appendBodyData(const char* start, size_t len)
{
    if(start == nullptr || len == 0)
    {
        return;
    }
    body_data_.insert(body_data_.end(),
        reinterpret_cast<const uint8_t*>(start),
        reinterpret_cast<const uint8_t*>(start + len));
}

void HttpResponse::appendBodyData(const std::string& data)
{
    appendBodyData(data.data(), data.size());
}

void HttpResponse::appendBodyData(const std::vector<char>& data)
{
    body_data_.insert(body_data_.end(), data.begin(), data.end());
}

void HttpResponse::appendBodyData(const std::vector<uint8_t>& data)
{
    body_data_.insert(body_data_.end(), data.begin(), data.end());
}

std::string HttpResponse::bodyString() const
{
    return std::string(body_data_.begin(), body_data_.end());
}

void HttpResponse::setJson(const nlohmann::json& root)
{
    setBodyData(root.dump());
    setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));
}

void HttpResponse::setText(const std::string& text)
{
    setBodyData(text);
    setContentMeta(MakeContentMeta(KnownMediaType::kTextPlain));
}

void HttpResponse::setOctetStream(std::vector<uint8_t> data)
{
    setBodyData(std::move(data));
    setContentMeta(MakeContentMeta(KnownMediaType::kApplicationOctetStream));
}

std::vector<uint8_t> HttpResponse::toBytes() const
{
    std::stringstream ss{""};
    ss << version_.toStr();
    ss << kSpace;
    ss << state_code_.toString();
    ss << kSpace;
    ss << state_code_.message();
    ss << kCRLF;

    auto headers = headers_;

    if(Version::kHttp11 == version_() && !connection_closed_)
    {
        SetOrReplaceHeader(headers, "Connection", "keep-alive");
        //对keep-alive模式参数配置
        SetOrReplaceHeader(headers, "Keep-Alive", "timeout=5, max=100");  // 连接保持5秒，最多100次请求

    }
    else
    {
        SetOrReplaceHeader(headers, "Connection", "close");
    }

    ContentMeta content_meta = content_meta_;
    if(!content_meta.media_type.empty()
        && IsTextLikeContent(content_meta)
        && !GetContentTypeParam(content_meta, "charset"))
    {
        SetContentTypeParam(content_meta, "charset", "utf-8");
    }

    const std::string content_type = ToContentTypeHeaderValue(content_meta);
    if(!content_type.empty())
    {
        SetOrReplaceHeader(headers, "Content-Type", content_type);
    }

    SetOrReplaceHeader(headers, "Content-Length", std::to_string(body_data_.size()));

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

std::string HttpResponse::toString()
{
    const auto bytes = toBytes();
    return std::string(bytes.begin(), bytes.end());
}

}
