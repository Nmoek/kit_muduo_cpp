/**
 * @file http_content.cpp
 * @brief HTTP报文体模型
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-20 17:23:19
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/net_log.h"
#include "net/http/http_content.h"

#include <cstring>

namespace kit_muduo::http {

namespace {

std::string Trim(const std::string& s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
    {
        return {};
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string ToLower(std::string s)
{
    for(auto& ch : s)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

ContentFormat ResolveContentFormatFromMediaType(const std::string &media_type)
{
    auto has_suffix = [&media_type](const std::string& suffix) {
        return media_type.size() >= suffix.size()
            && media_type.compare(media_type.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    const bool is_application_media =
        media_type.compare(0, std::strlen("application/"), "application/") == 0;

    if(media_type == "application/json" || (is_application_media && has_suffix("+json")))
    {
        return ContentFormat::kJson;
    }
    else if(media_type == "multipart/form-data")
    {
        return ContentFormat::kMultipartFormData;
    }
    else if(media_type == "application/xml"
        || media_type == "text/xml"
        || (is_application_media && has_suffix("+xml")))
    {
        return ContentFormat::kXml;
    }
    else if(media_type == "text/plain")
    {
        return ContentFormat::kPlainText;
    }
    else if(media_type == "application/octet-stream")
    {
        return ContentFormat::kOctetStream;
    }

    return ContentFormat::kUnknown;
}


void GetContentParams(size_t semi, ContentMeta &meta)
{
    const std::string &raw_content_type = meta.raw_content_type;
    size_t start = 0;

    while(semi != std::string::npos)
    {
        start = semi + 1;
        semi = raw_content_type.find(';', start);

        std::string item = raw_content_type.substr(
            start,
            semi == std::string::npos ? std::string::npos : semi - start);

        size_t eq = item.find('=');
        if(eq == std::string::npos)
        {
            continue;
        }

        std::string key = ToLower(Trim(item.substr(0, eq)));

        std::string value = Trim(item.substr(eq + 1));

        if(value.size() >= 2 && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }

        if(!key.empty())
        {
            meta.params[key] = value;
        }
    }
}

ContentMeta ParseContentTypeHelper(const std::string &raw_content_type, bool is_default)
{
    ContentMeta meta;
    meta.raw_content_type = raw_content_type;

    if(raw_content_type.empty())
    {
        meta.format = is_default ? ContentFormat::kPlainText : ContentFormat::kUnknown;
        meta.media_type = is_default ? "text/plain" : "";
        return meta;
    }


    size_t semi = raw_content_type.find(';');

    meta.media_type = ToLower(Trim(raw_content_type.substr(0, semi)));

    if(meta.media_type.empty())
    {
        meta.format = is_default ? ContentFormat::kPlainText : ContentFormat::kUnknown;
        meta.media_type = is_default ? "text/plain" : "";
    }
    else
    {
        meta.format = ResolveContentFormatFromMediaType(meta.media_type);
    }

    HTTP_F_DEBUG("raw_content_type: %s,  media_type: %s\n", raw_content_type.c_str(), meta.media_type.c_str());
    
    GetContentParams(semi, meta);

    return meta;
}

}



ContentMeta ParseHttpContentType(const std::string &raw_content_type)
{
    return ParseContentTypeHelper(raw_content_type, false);
}

ContentMeta ParseMultiformPartContentType(const std::string& raw_content_type)
{
    return ParseContentTypeHelper(raw_content_type, true);
}







} // kit_muduo::http
