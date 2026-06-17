/**
 * @file content_codec.h
 * @brief 基础数据格式序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-15 11:33:12
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_CONTENT_CODEC_H__
#define __KIT_CONTENT_CODEC_H__

#include "base/content_parser.h"
#include "nlohmann/json.hpp"

#include <exception>
#include <initializer_list>
#include <string>
#include <unordered_map>

namespace kit_muduo {

enum class ContentFormat
{
    kUnknown,
    kJson,
    kMultipart,
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
    const char* data{nullptr};
    size_t size{0};
    ContentMeta meta;
};

enum class ContentCodecErrorCode
{
    kOk,
    kEmptyContent,
    kUnsupportedFormat,
    kInvalidContentType,
    kDecodeFailed,
    kEncodeFailed,
    kMissingField,
    kInvalidField,
    kUnsupportedTarget,
    kInternalError,
};

struct ContentCodecResult
{
    bool ok{false};
    ContentCodecErrorCode code{ContentCodecErrorCode::kOk};
    std::string message;
    std::string field;

    static ContentCodecResult Success()
    {
        return {true, ContentCodecErrorCode::kOk, "success", ""};
    }

    static ContentCodecResult Failed(ContentCodecErrorCode code, const std::string& msg = "codec error", const std::string& field = "")
    {
        return {false, code, msg, field};
    }

};

/**
 * @brief 将Http头部字段中'Content-Type'统一转换
 * @param raw_content_type  multipart/form-data; boundary=...
 * @return ContentMeta 
 */
ContentMeta ParseContentMetaFromHttpHeader(std::string raw_content_type);



template<typename T, ContentFormat Format>
struct ContentDecoder
{
    static ContentCodecResult Decode(const ContentView&, T*)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedTarget, "decoder unsupported");
    }
};

template<typename T>
struct ContentDecoder<T, ContentFormat::kJson>
{
    static ContentCodecResult Decode(const ContentView& view, T* out)
    {
        try {
            nlohmann::json::parse(view.data, view.data + view.size).get_to<T>(*out);

        }catch(const std::exception &e) {

            return ContentCodecResult::Failed(ContentCodecErrorCode::kDecodeFailed, std::string("json decode failed: ") + e.what());
        }

        return ContentCodecResult::Success();
    }
};

//  TODO xml缺库
template<typename T>
struct ContentDecoder<T, ContentFormat::kXml>
{
    static ContentCodecResult Decode(const ContentView& view, T* out)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedFormat);
    }
};


/*****************MultipartForm 格式********** */
/**
 * @brief 注意: 这里需要结合业务DTO进行序列化/反序列化绑定。将绑定放在具体的业务方中 AddProtocol等
 * @tparam T 
 */
template<typename T>
struct MultipartObjectBinder
{
    static ContentCodecResult Bind(const MultiFormParser::PartMap& parts, T* out)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedTarget,  "multipart binder target unsupported");
    }
};


template<typename T>
struct ContentDecoder<T, ContentFormat::kMultipart>
{
    static ContentCodecResult Decode(const ContentView &view, T *out)
    {
        auto it = view.meta.params.find("boundary");
        if(it == view.meta.params.end() || it->second.empty())
        {
            return ContentCodecResult::Failed(
                ContentCodecErrorCode::kInvalidContentType,
                "multipart boundary missing");
        }

        try {

            auto parts = MultiFormParser::parse(view.data, view.size, view.meta.raw_content_type);

            return MultipartObjectBinder<T>::Bind(parts, out);

        }catch(const std::exception &e) {

            return ContentCodecResult::Failed(
                ContentCodecErrorCode::kDecodeFailed,
                std::string("multipart decode failed: ") + e.what());

        }
    }
};

/**
 * @brief 根据part部分的name进行匹配
 * @param parts 
 * @param field_name 
 * @param part 
 * @return ContentCodecResult 
 */
inline ContentCodecResult FindRequirePart(const MultiFormParser::PartMap &parts, const std::string& field_name, FormPart *&part)
{
    auto it = parts.find(field_name);
    if(it == parts.end())
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kMissingField, "missing multipart field: " + field_name, field_name);
    }
    part = const_cast<FormPart*>(&it->second);
    return ContentCodecResult::Success();
}

template<typename T>
ContentCodecResult ParseJsonPartToObject(const MultiFormParser::PartMap &parts, const std::string &field_name, T *out)
{
    FormPart *part = nullptr;
    auto result = FindRequirePart(parts, field_name, part);
    if(!result.ok || !part)
    {
        return result;
    }

    try {

        nlohmann::json::parse(part->data).get_to<T>(*out);

    } catch(const std::exception &e) {

        return ContentCodecResult::Failed(ContentCodecErrorCode::kInvalidField, std::string("invalid json form field: ") + e.what(), field_name);
    }

    return ContentCodecResult::Success();
}


ContentCodecResult ParseJsonPartToRaw(const MultiFormParser::PartMap &parts, const std::string &field_name, nlohmann::json &out_json);


ContentCodecResult ParseOctetStreamPartToRaw(const MultiFormParser::PartMap &parts, const std::string &field_name, std::vector<char> &out, bool required = true);



/*****************MultipartForm 格式********** */

template<typename T>
class ContentDecodePipeline
{
public:
    static ContentCodecResult Decode(const ContentView &view, T *out, std::initializer_list<ContentFormat> allowed_formats)
    {
        if(!out)
        {
            return ContentCodecResult::Failed(ContentCodecErrorCode::kInternalError, "decode target is null");
        }
        if(!view.data && view.size > 0)
        {
            return ContentCodecResult::Failed(ContentCodecErrorCode::kInternalError, "data is null");
        }
        if(!IsAllowed(view.meta.format, allowed_formats))
        {
            return ContentCodecResult::Failed(
                ContentCodecErrorCode::kUnsupportedFormat,
                "unsupported content format");
        }

        switch (view.meta.format) 
        {
            case ContentFormat::kJson: return ContentDecoder<T, ContentFormat::kJson>::Decode(view, out);
            case ContentFormat::kMultipart: return ContentDecoder<T, ContentFormat::kMultipart>::Decode(view, out);
            default:
                return ContentCodecResult::Failed(
                    ContentCodecErrorCode::kUnsupportedFormat,
                    "content format decoder not enabled");
        }
        
    }
private:
    static bool IsAllowed(ContentFormat actual_format, std::initializer_list<ContentFormat> allowed_formats)
    {
        for(const auto f : allowed_formats)
        {
            if(f == actual_format)
            {
                return  true;
            }
        }
        return false;
    }

};


/***************TODO 序列化能力预留****************** */

template<typename T, ContentFormat Format>
struct ContentEncoder
{
    static ContentCodecResult Encode(const T&, std::vector<char>&)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedTarget, "encoder unsupported");
    }
};

template<typename T>
struct ContentEncoder<T, ContentFormat::kJson>
{
    static ContentCodecResult Encode(const T& in, std::vector<char>& out_data)
    {
        try {

            nlohmann::json root = in;
            const std::string& tmp = root.dump();
            out_data.assign(tmp.begin(), tmp.end());

        } catch(const std::exception &e) {

            return ContentCodecResult::Failed(ContentCodecErrorCode::kEncodeFailed, std::string("json encode invalid ") + e.what());
        }

        return ContentCodecResult::Success();
    }
};


/***************TODO 序列化能力预留****************** */

}
#endif //__KIT_CONTENT_CODEC_H__
