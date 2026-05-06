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
#include "net/http/http_util.h"
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
    headers_[head] = val;
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
    headers_[head] = val;
    return true;
}

std::string HttpResponse::getHeader(const std::string &key) const
{
    auto it = headers_.find(key);
    return it == headers_.end() ? "" : it->second;
}

std::string HttpResponse::toString()
{
    std::stringstream ss{""};
    ss << version_.toString();
    ss << kSpace;
    ss << state_code_.toString();
    ss << kSpace;
    ss << state_code_.message();
    ss << kCRLF;

    if(Version::kHttp11 == version_() && !connection_closed_)
    {
        headers_["Connection"] = "keep-alive";
        //对keep-alive模式参数配置
        headers_["Keep-Alive"] = "timeout=5, max=100";  // 连接保持5秒，最多100次请求

    }
    else
    {
        headers_["Connection"] = "close";
    }

    ContentType content_type = body_.contentType();
    if(ContentType::kUnknowType != content_type.toInt())
    {
        // 默认字符集 utf-8
        std::string content_type_str = content_type.toString();
        content_type_str += "; ";
        content_type_str += "charset=utf-8";
        if(ContentType::kMultiForm == content_type.toInt())
        {
            content_type_str += "; ";
            content_type_str += "boundary=----WebKitFormBoundaryNQJ0YrO2NeaUfM7n";
        }
        headers_["Content-Type"] = content_type_str;
    }


    if(body_.data().size())
    {
        headers_["Content-Length"] = std::to_string(body_.data().size());
    }

    for(auto &it : headers_)
    {
        ss << it.first;
        ss << kColon << kSpace;
        ss << it.second;
        ss << kCRLF;
    }
    ss << kCRLF;
    ss << body_.toString(); // TODO 这里是有问题的 不能认为Body一直是string类型
    return ss.str();
}

}
