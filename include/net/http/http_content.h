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

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace kit_domain {
enum class ProtocolBodyType;
}

namespace kit_muduo::http {

struct MediaType
{
    std::string type;       // application / text / image
    std::string subtype;   // json / html / svg+xml / vnd.api+json
    std::string suffix;    // json / xml，可选
};

enum class KnownMediaType
{
    kUnknown,
    // application
    kApplicationJson,
    kApplicationProblemJson,
    kApplicationXml,
    kApplicationSoapXml,
    kApplicationFormUrlEncoded,
    kApplicationOctetStream,
    kApplicationJavascript,
    kApplicationWasm,
    kApplicationPdf,
    kApplicationZip,
    kApplicationGzip,

    // multipart
    kMultipartFormData,

    // text
    kTextPlain,
    kTextHtml,
    kTextCss,
    kTextCsv,
    kTextXml,
    kTextJavascript,

    // image
    kImageJpeg,
    kImagePng,
    kImageGif,
    kImageWebp,
    kImageIcon,
    kImageSvgXml,
    kImageAvif,
    kAudioMpeg,
    kVideoMp4,
    kFontWoff,
    kFontWoff2,
    kCustom,
};


enum class ContentCodecFormat
{
    kNone,
    kJson,
    kMultipartFormData,
    kXml,
    kText,
    kFormUrlEncoded,
    kBinary,
};


struct ContentMeta
{
    /// @brief 当前项目明确支持或经常使用的 media type 标识
    KnownMediaType known_type{KnownMediaType::kUnknown};
    /// @brief 原始 header 值(便于日志、调试、兼容特殊参数)
    std::string raw_content_type;
    /// @brief 规范化后的 type/subtype 统一转小写保存
    std::string media_type;
    /// @brief 解析后的 HTTP media type 结构
    MediaType parsed_media_type;
    /// @brief 附加参数
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


std::string TrimHttpToken(const std::string& value);

std::string ToLowerAscii(std::string value);

std::string NormalizeMediaType(const std::string& media_type);

bool IsValidMediaType(const std::string& media_type);

MediaType ParseMediaTypeFromString(const std::string& media_type);

std::string ToMediaTypeString(const MediaType& media_type);

KnownMediaType ResolveKnownMediaType(const MediaType& media_type);

ContentCodecFormat ResolveContentCodecFormat(const ContentMeta& meta);

ContentMeta MakeContentMeta(KnownMediaType known_type);

ContentMeta MakeContentMetaFromMediaType(const std::string& media_type);

std::string ToContentTypeHeaderValue(const ContentMeta& meta);

const std::string* GetContentTypeParam(const ContentMeta& meta, const std::string& key);

void SetContentTypeParam(ContentMeta& meta, const std::string& key, const std::string& value);

bool IsTextLikeContent(const ContentMeta& meta);

bool IsJsonLikeContent(const ContentMeta& meta);

bool IsXmlLikeContent(const ContentMeta& meta);

std::string GuessMediaTypeFromExtension(const std::string& path_or_extension);

kit_domain::ProtocolBodyType GuessProtocolBodyTypeFromContentMeta(const ContentMeta& meta);

const std::unordered_map<std::string, std::string>& BuiltinMimeTypesByExtension();

bool RegisterMimeTypeForExtension(const std::string& extension,
    const std::string& media_type);



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
