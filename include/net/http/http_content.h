/**
 * @file http_content.h
 * @brief HTTP报文体模型
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-20 17:21:39
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_HTTP_CONTENT_H__
#define __KIT_HTTP_CONTENT_H__

#include "base/bytes_codec.h"

#include <string>
#include <unordered_map>

namespace kit_muduo::http {


enum class ContentFormat
{
    kUnknown,
    kJson,
    kMultipartFormData,
    kXml,
    kPlainText,
    kOctetStream,
};


struct ContentMeta
{
    ContentFormat format{ContentFormat::kUnknown};
    std::string raw_content_type;
    std::string media_type;
    std::unordered_map<std::string, std::string> params;

};

struct ContentView
{
    const uint8_t* data{nullptr};
    size_t size{0};
    ContentMeta meta;

    kit_muduo::BytesView toBytesView() const 
    {
        return {
            .data = data,
            .size = size,
        };
    }
};



/*
    注意: 这两个接口区分的是'Content-Type'缺省时候的区别
*/

/**
 * @brief 将Http头部字段中'Content-Type'统一转换
 * @param raw_content_type  multipart/form-data; boundary=...
 * @return ContentMeta 
 */
ContentMeta ParseHttpContentType(const std::string &raw_content_type);

/**
 * @brief 将Multiform头部字段中'Content-Type'统一转换
 * @param raw_content_type 
 * @return ContentMeta 
 */
ContentMeta ParseMultiformPartContentType(const std::string &raw_content_type);



}
#endif //__KIT_HTTP_CONTENT_H__