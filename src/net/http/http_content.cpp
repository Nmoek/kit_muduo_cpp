/**
 * @file http_content.cpp
 * @brief HTTP报文体模型
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-20 17:23:19
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/http/http_content.h"
#include "net/net_log.h"
#include "domain/type.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>


using namespace kit_domain;

namespace kit_muduo::http {

namespace {

bool HasPrefix(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size()
        && value.compare(0, prefix.size(), prefix) == 0;
}

std::string KnownMediaTypeToString(KnownMediaType known_type)
{
    switch(known_type)
    {
        case KnownMediaType::kApplicationJson: return "application/json";
        case KnownMediaType::kApplicationProblemJson: return "application/problem+json";
        case KnownMediaType::kApplicationXml: return "application/xml";
        case KnownMediaType::kApplicationSoapXml: return "application/soap+xml";
        case KnownMediaType::kApplicationFormUrlEncoded: return "application/x-www-form-urlencoded";
        case KnownMediaType::kApplicationOctetStream: return "application/octet-stream";
        case KnownMediaType::kApplicationJavascript: return "application/javascript";
        case KnownMediaType::kApplicationWasm: return "application/wasm";
        case KnownMediaType::kApplicationPdf: return "application/pdf";
        case KnownMediaType::kApplicationZip: return "application/zip";
        case KnownMediaType::kApplicationGzip: return "application/gzip";
        case KnownMediaType::kMultipartFormData: return "multipart/form-data";
        case KnownMediaType::kTextPlain: return "text/plain";
        case KnownMediaType::kTextHtml: return "text/html";
        case KnownMediaType::kTextCss: return "text/css";
        case KnownMediaType::kTextCsv: return "text/csv";
        case KnownMediaType::kTextXml: return "text/xml";
        case KnownMediaType::kTextJavascript: return "text/javascript";
        case KnownMediaType::kImageJpeg: return "image/jpeg";
        case KnownMediaType::kImagePng: return "image/png";
        case KnownMediaType::kImageGif: return "image/gif";
        case KnownMediaType::kImageWebp: return "image/webp";
        case KnownMediaType::kImageIcon: return "image/x-icon";
        case KnownMediaType::kImageSvgXml: return "image/svg+xml";
        case KnownMediaType::kImageAvif: return "image/avif";
        case KnownMediaType::kAudioMpeg: return "audio/mpeg";
        case KnownMediaType::kVideoMp4: return "video/mp4";
        case KnownMediaType::kFontWoff: return "font/woff";
        case KnownMediaType::kFontWoff2: return "font/woff2";
        default: return "";
    }
}

bool IsHttpTokenChar(unsigned char ch)
{
    if(std::isalnum(ch) != 0)
    {
        return true;
    }

    switch(ch)
    {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            return true;
        default:
            return false;
    }
}

bool NeedQuoteParamValue(const std::string& value)
{
    if(value.empty())
    {
        return true;
    }

    for(const auto ch : value)
    {
        if(IsHttpTokenChar(static_cast<unsigned char>(ch)))
        {
            continue;
        }
        return true;
    }

    return false;
}

std::string QuoteParamValueIfNeeded(const std::string& value)
{
    if(!NeedQuoteParamValue(value))
    {
        return value;
    }

    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for(const auto ch : value)
    {
        if(ch == '"' || ch == '\\')
        {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string UnquoteParamValue(const std::string& value)
{
    if(value.size() < 2 || value.front() != '"' || value.back() != '"')
    {
        return value;
    }

    std::string out;
    out.reserve(value.size() - 2);
    bool escaped = false;
    for(size_t i = 1; i + 1 < value.size(); ++i)
    {
        const char ch = value[i];
        if(escaped)
        {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if(ch == '\\')
        {
            escaped = true;
            continue;
        }
        out.push_back(ch);
    }
    if(escaped)
    {
        out.push_back('\\');
    }
    return out;
}

size_t FindParamSeparator(const std::string& value, size_t start)
{
    bool in_quote = false;
    bool escaped = false;

    for(size_t i = start; i < value.size(); ++i)
    {
        const char ch = value[i];
        if(escaped)
        {
            escaped = false;
            continue;
        }
        if(in_quote && ch == '\\')
        {
            escaped = true;
            continue;
        }
        if(ch == '"')
        {
            in_quote = !in_quote;
            continue;
        }
        if(!in_quote && ch == ';')
        {
            return i;
        }
    }

    return std::string::npos;
}

std::string NormalizeExtensionName(const std::string& extension)
{
    std::string value = TrimHttpToken(extension);
    if(value.empty())
    {
        return "";
    }

    if(value.find_first_of("/\\?#") != std::string::npos)
    {
        return "";
    }

    if(value.front() != '.')
    {
        value = "." + value;
    }

    const auto dot_pos = value.find_last_of('.');
    if(dot_pos == std::string::npos || dot_pos + 1 >= value.size())
    {
        return "";
    }

    return ToLowerAscii(value.substr(dot_pos));
}

std::string ExtractExtensionFromPathOrName(const std::string& path_or_extension)
{
    std::string value = TrimHttpToken(path_or_extension);
    if(value.empty())
    {
        return "";
    }

    const auto query_pos = value.find_first_of("?#");
    if(query_pos != std::string::npos)
    {
        value = value.substr(0, query_pos);
    }

    const auto slash_pos = value.find_last_of("/\\");
    const bool has_path_separator = slash_pos != std::string::npos;
    if(has_path_separator)
    {
        value = value.substr(slash_pos + 1);
    }

    if(value.empty())
    {
        return "";
    }

    const auto dot_pos = value.find_last_of('.');
    if(dot_pos != std::string::npos && dot_pos + 1 < value.size())
    {
        return ToLowerAscii(value.substr(dot_pos));
    }

    if(!has_path_separator)
    {
        return NormalizeExtensionName(value);
    }

    return "";
}

std::unordered_map<std::string, std::string>& RuntimeMimeTypesByExtension()
{
    static std::unordered_map<std::string, std::string> mappings;
    return mappings;
}

void ParseContentTypeParams(const std::string& raw_content_type,
    size_t semi,
    ContentMeta& meta)
{
    while(semi != std::string::npos)
    {
        const size_t start = semi + 1;
        semi = FindParamSeparator(raw_content_type, start);
        std::string item = raw_content_type.substr(
            start,
            semi == std::string::npos ? std::string::npos : semi - start);

        const size_t eq = item.find('=');
        if(eq == std::string::npos)
        {
            continue;
        }

        const std::string key = ToLowerAscii(TrimHttpToken(item.substr(0, eq)));
        const std::string value = UnquoteParamValue(TrimHttpToken(item.substr(eq + 1)));
        if(!key.empty())
        {
            meta.params[key] = value;
        }
    }
}

ContentMeta ParseContentTypeHelper(const std::string& raw_content_type, bool default_to_text_plain)
{
    ContentMeta meta;
    meta.raw_content_type = raw_content_type;

    if(raw_content_type.empty())
    {
        if(default_to_text_plain)
        {
            meta = MakeContentMeta(KnownMediaType::kTextPlain);
            meta.raw_content_type.clear();
        }
        return meta;
    }

    const size_t semi = raw_content_type.find(';');
    const std::string media_type = NormalizeMediaType(raw_content_type.substr(0, semi));
    if(media_type.empty() || !IsValidMediaType(media_type))
    {
        if(default_to_text_plain)
        {
            meta = MakeContentMeta(KnownMediaType::kTextPlain);
            meta.raw_content_type = raw_content_type;
        }
        return meta;
    }

    meta.media_type = media_type;
    meta.parsed_media_type = ParseMediaTypeFromString(media_type);
    meta.known_type = ResolveKnownMediaType(meta.parsed_media_type);
    ParseContentTypeParams(raw_content_type, semi, meta);

    HTTP_F_DEBUG("raw_content_type: %s,  media_type: %s\n", raw_content_type.c_str(), meta.media_type.c_str());
    return meta;
}

} // namespace

std::string TrimHttpToken(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
    {
        return {};
    }
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string ToLowerAscii(std::string value)
{
    for(auto& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string NormalizeMediaType(const std::string& media_type)
{
    return ToLowerAscii(TrimHttpToken(media_type));
}

bool IsValidMediaType(const std::string& media_type)
{
    const std::string normalized = NormalizeMediaType(media_type);
    const size_t slash_pos = normalized.find('/');
    if(slash_pos == std::string::npos
        || slash_pos == 0
        || slash_pos + 1 >= normalized.size()
        || normalized.find('/', slash_pos + 1) != std::string::npos)
    {
        return false;
    }

    for(size_t i = 0; i < normalized.size(); ++i)
    {
        const char ch = normalized[i];
        if(ch == '/')
        {
            continue;
        }
        if(!IsHttpTokenChar(static_cast<unsigned char>(ch)))
        {
            return false;
        }
    }

    return true;
}

MediaType ParseMediaTypeFromString(const std::string& media_type)
{
    MediaType parsed;
    const std::string normalized = NormalizeMediaType(media_type);
    if(normalized.empty() || !IsValidMediaType(normalized))
    {
        return parsed;
    }

    const size_t slash_pos = normalized.find('/');
    parsed.type = normalized.substr(0, slash_pos);
    parsed.subtype = normalized.substr(slash_pos + 1);

    const size_t plus_pos = parsed.subtype.rfind('+');
    if(plus_pos != std::string::npos && plus_pos + 1 < parsed.subtype.size())
    {
        parsed.suffix = parsed.subtype.substr(plus_pos + 1);
    }

    return parsed;
}

std::string ToMediaTypeString(const MediaType& media_type)
{
    if(media_type.type.empty() || media_type.subtype.empty())
    {
        return "";
    }
    return media_type.type + "/" + media_type.subtype;
}

KnownMediaType ResolveKnownMediaType(const MediaType& media_type)
{
    const std::string value = ToMediaTypeString(media_type);
    if(value.empty())
    {
        return KnownMediaType::kUnknown;
    }

    if(value == "application/json") return KnownMediaType::kApplicationJson;
    if(value == "application/problem+json") return KnownMediaType::kApplicationProblemJson;
    if(value == "application/xml") return KnownMediaType::kApplicationXml;
    if(value == "application/soap+xml") return KnownMediaType::kApplicationSoapXml;
    if(value == "application/x-www-form-urlencoded") return KnownMediaType::kApplicationFormUrlEncoded;
    if(value == "application/octet-stream") return KnownMediaType::kApplicationOctetStream;
    if(value == "application/javascript") return KnownMediaType::kApplicationJavascript;
    if(value == "application/x-javascript") return KnownMediaType::kApplicationJavascript;
    if(value == "application/wasm") return KnownMediaType::kApplicationWasm;
    if(value == "application/pdf") return KnownMediaType::kApplicationPdf;
    if(value == "application/zip") return KnownMediaType::kApplicationZip;
    if(value == "application/gzip") return KnownMediaType::kApplicationGzip;
    if(value == "multipart/form-data") return KnownMediaType::kMultipartFormData;
    if(value == "text/plain") return KnownMediaType::kTextPlain;
    if(value == "text/html") return KnownMediaType::kTextHtml;
    if(value == "text/css") return KnownMediaType::kTextCss;
    if(value == "text/csv") return KnownMediaType::kTextCsv;
    if(value == "text/xml") return KnownMediaType::kTextXml;
    if(value == "text/javascript") return KnownMediaType::kTextJavascript;
    if(value == "image/jpeg") return KnownMediaType::kImageJpeg;
    if(value == "image/png") return KnownMediaType::kImagePng;
    if(value == "image/gif") return KnownMediaType::kImageGif;
    if(value == "image/webp") return KnownMediaType::kImageWebp;
    if(value == "image/x-icon") return KnownMediaType::kImageIcon;
    if(value == "image/vnd.microsoft.icon") return KnownMediaType::kImageIcon;
    if(value == "image/svg+xml") return KnownMediaType::kImageSvgXml;
    if(value == "image/avif") return KnownMediaType::kImageAvif;
    if(value == "audio/mpeg") return KnownMediaType::kAudioMpeg;
    if(value == "video/mp4") return KnownMediaType::kVideoMp4;
    if(value == "font/woff") return KnownMediaType::kFontWoff;
    if(value == "font/woff2") return KnownMediaType::kFontWoff2;

    return KnownMediaType::kCustom;
}

ContentCodecFormat ResolveContentCodecFormat(const ContentMeta& meta)
{
    switch(meta.known_type)
    {
        case KnownMediaType::kApplicationJson:
        case KnownMediaType::kApplicationProblemJson:
            return ContentCodecFormat::kJson;
        case KnownMediaType::kMultipartFormData:
            return ContentCodecFormat::kMultipartFormData;
        case KnownMediaType::kApplicationXml:
        case KnownMediaType::kApplicationSoapXml:
        case KnownMediaType::kTextXml:
            return ContentCodecFormat::kXml;
        case KnownMediaType::kTextPlain:
        case KnownMediaType::kTextHtml:
        case KnownMediaType::kTextCss:
        case KnownMediaType::kTextCsv:
        case KnownMediaType::kTextJavascript:
        case KnownMediaType::kApplicationJavascript:
        case KnownMediaType::kImageSvgXml:
            return ContentCodecFormat::kText;
        case KnownMediaType::kApplicationFormUrlEncoded:
            return ContentCodecFormat::kFormUrlEncoded;
        case KnownMediaType::kApplicationOctetStream:
        case KnownMediaType::kApplicationWasm:
        case KnownMediaType::kApplicationPdf:
        case KnownMediaType::kApplicationZip:
        case KnownMediaType::kApplicationGzip:
        case KnownMediaType::kImageJpeg:
        case KnownMediaType::kImagePng:
        case KnownMediaType::kImageGif:
        case KnownMediaType::kImageWebp:
        case KnownMediaType::kImageIcon:
        case KnownMediaType::kImageAvif:
        case KnownMediaType::kAudioMpeg:
        case KnownMediaType::kVideoMp4:
        case KnownMediaType::kFontWoff:
        case KnownMediaType::kFontWoff2:
            return ContentCodecFormat::kBinary;
        case KnownMediaType::kCustom:
            if(meta.parsed_media_type.suffix == "json")
            {
                return ContentCodecFormat::kJson;
            }
            if(meta.parsed_media_type.suffix == "xml" && meta.media_type != "image/svg+xml")
            {
                return ContentCodecFormat::kXml;
            }
            if(HasPrefix(meta.media_type, "text/"))
            {
                return ContentCodecFormat::kText;
            }
            return ContentCodecFormat::kNone;
        default:
            return ContentCodecFormat::kNone;
    }
}

ContentMeta MakeContentMeta(KnownMediaType known_type)
{
    ContentMeta meta;
    meta.known_type = known_type;
    meta.media_type = KnownMediaTypeToString(known_type);
    meta.parsed_media_type = ParseMediaTypeFromString(meta.media_type);
    if(meta.media_type.empty())
    {
        meta.known_type = KnownMediaType::kUnknown;
    }
    return meta;
}

ContentMeta MakeContentMetaFromMediaType(const std::string& media_type)
{
    ContentMeta meta;
    meta.media_type = NormalizeMediaType(media_type);
    if(meta.media_type.empty() || !IsValidMediaType(meta.media_type))
    {
        meta.media_type.clear();
        return meta;
    }
    meta.parsed_media_type = ParseMediaTypeFromString(meta.media_type);
    meta.known_type = ResolveKnownMediaType(meta.parsed_media_type);
    return meta;
}

std::string ToContentTypeHeaderValue(const ContentMeta& meta)
{
    std::string media_type = meta.media_type;
    if(media_type.empty())
    {
        media_type = ToMediaTypeString(meta.parsed_media_type);
    }
    if(media_type.empty())
    {
        return "";
    }

    std::vector<std::pair<std::string, std::string>> params;
    params.reserve(meta.params.size());
    for(const auto& it : meta.params)
    {
        const std::string key = ToLowerAscii(TrimHttpToken(it.first));
        if(!key.empty())
        {
            params.emplace_back(key, it.second);
        }
    }
    std::sort(params.begin(), params.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    std::ostringstream oss;
    oss << media_type;
    for(const auto& param : params)
    {
        oss << "; " << param.first << '=' << QuoteParamValueIfNeeded(param.second);
    }
    return oss.str();
}

const std::string* GetContentTypeParam(const ContentMeta& meta, const std::string& key)
{
    const std::string normalized_key = ToLowerAscii(TrimHttpToken(key));
    auto it = meta.params.find(normalized_key);
    return it == meta.params.end() ? nullptr : &it->second;
}

void SetContentTypeParam(ContentMeta& meta, const std::string& key, const std::string& value)
{
    const std::string normalized_key = ToLowerAscii(TrimHttpToken(key));
    if(!normalized_key.empty())
    {
        meta.params[normalized_key] = value;
    }
}

bool IsTextLikeContent(const ContentMeta& meta)
{
    const auto codec_format = ResolveContentCodecFormat(meta);
    if(codec_format == ContentCodecFormat::kJson
        || codec_format == ContentCodecFormat::kXml
        || codec_format == ContentCodecFormat::kText
        || codec_format == ContentCodecFormat::kFormUrlEncoded)
    {
        return true;
    }

    if(HasPrefix(meta.media_type, "text/"))
    {
        return true;
    }
    if(meta.media_type == "application/javascript"
        || meta.media_type == "application/x-javascript"
        || meta.media_type == "application/x-www-form-urlencoded"
        || meta.media_type == "image/svg+xml")
    {
        return true;
    }
    if(meta.parsed_media_type.suffix == "json")
    {
        return true;
    }
    if(meta.parsed_media_type.suffix == "xml" && meta.media_type != "image/svg+xml")
    {
        return true;
    }

    return false;
}

bool IsJsonLikeContent(const ContentMeta& meta)
{
    return ResolveContentCodecFormat(meta) == ContentCodecFormat::kJson;
}

bool IsXmlLikeContent(const ContentMeta& meta)
{
    return ResolveContentCodecFormat(meta) == ContentCodecFormat::kXml;
}

std::string GuessMediaTypeFromExtension(const std::string& path_or_extension)
{
    const std::string extension = ExtractExtensionFromPathOrName(path_or_extension);
    if(extension.empty())
    {
        return "application/octet-stream";
    }

    auto& runtime_mappings = RuntimeMimeTypesByExtension();
    auto runtime_it = runtime_mappings.find(extension);
    if(runtime_it != runtime_mappings.end())
    {
        return runtime_it->second;
    }

    const auto& builtin_mappings = BuiltinMimeTypesByExtension();
    auto builtin_it = builtin_mappings.find(extension);
    if(builtin_it != builtin_mappings.end())
    {
        return builtin_it->second;
    }

    return "application/octet-stream";
}

ProtocolBodyType GuessProtocolBodyTypeFromContentMeta(const ContentMeta& meta)
{
    const auto codec_type = ResolveContentCodecFormat(meta);
    switch (codec_type) 
    {
        case ContentCodecFormat::kNone:
            return ProtocolBodyType::kNone;
        case ContentCodecFormat::kJson:
            return ProtocolBodyType::kJson;
        case ContentCodecFormat::kXml:
            return ProtocolBodyType::kXml;
        case ContentCodecFormat::kText:
        case ContentCodecFormat::kFormUrlEncoded:
            return ProtocolBodyType::kText;
        case ContentCodecFormat::kMultipartFormData:
            return ProtocolBodyType::kMultiForm;
        default:
            return ProtocolBodyType::kBinary;
    }
}

const std::unordered_map<std::string, std::string>& BuiltinMimeTypesByExtension()
{
    static const std::unordered_map<std::string, std::string> mappings{
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "text/javascript"},
        {".mjs", "text/javascript"},
        {".json", "application/json"},
        {".xml", "application/xml"},
        {".txt", "text/plain"},
        {".log", "text/plain"},
        {".svg", "image/svg+xml"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".ico", "image/x-icon"},
        {".wasm", "application/wasm"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".gz", "application/gzip"},
        {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".csv", "text/csv"},
        {".avif", "image/avif"},
    };
    return mappings;
}

bool RegisterMimeTypeForExtension(const std::string& extension, const std::string& media_type)
{
    const std::string normalized_extension = NormalizeExtensionName(extension);
    const std::string normalized_media_type = NormalizeMediaType(media_type);
    if(normalized_extension.empty() || !IsValidMediaType(normalized_media_type))
    {
        return false;
    }

    RuntimeMimeTypesByExtension()[normalized_extension] = normalized_media_type;
    return true;
}

ContentMeta ParseHttpContentType(const std::string& raw_content_type)
{
    return ParseContentTypeHelper(raw_content_type, false);
}

ContentMeta ParseMultiformPartContentType(const std::string& raw_content_type)
{
    return ParseContentTypeHelper(raw_content_type, true);
}

} // namespace kit_muduo::http
