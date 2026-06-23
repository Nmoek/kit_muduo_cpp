/**
 * @file http_content_codec.h
 * @brief HTTP报文体序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-20 15:00:18
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_HTTP_CONTENT_CODEC_H__
#define __KIT_HTTP_CONTENT_CODEC_H__

#include "base/bytes_codec.h"
#include "net/http/http_content.h"
#include "net/http/multiform.h"

#include <string>

namespace kit_muduo::http {


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
 * @brief 通用数据格式错误 --> Http Content 格式错误
 * @param result 
 * @return ContentCodecResult 
 */
ContentCodecResult ToHttpContentResult(const kit_muduo::CodecResult &result);



template<typename T, ContentCodecFormat Format>
struct ContentDecoder
{
    static ContentCodecResult Decode(const ContentView&, T&)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedTarget, "decoder unsupported");
    }
};

template<typename T>
struct ContentDecoder<T, ContentCodecFormat::kJson>
{
    static ContentCodecResult Decode(const ContentView& view, T& out)
    {
        return ToHttpContentResult(kit_muduo::JsonCodec::Decode(view.toBytesView(), out));
    }
};

template<>
struct ContentDecoder<MultiForm, ContentCodecFormat::kMultipartFormData>
{
    static ContentCodecResult Decode(const ContentView &view, MultiForm& form)
    {
        try {
            auto it = view.meta.params.find("boundary");
            if(it == view.meta.params.end() || it->second.empty())
            {
                return ContentCodecResult::Failed(ContentCodecErrorCode::kInvalidContentType, "multipart boundary missing");
            }

            form = MultiForm::parse(view.data, view.size, it->second);

        }catch(const std::exception &e) {

            return ContentCodecResult::Failed(
                ContentCodecErrorCode::kDecodeFailed,
                std::string("multipart decode failed: ") + e.what());

        }
        return ContentCodecResult::Success();
    }
};

template<typename T>
struct ContentDecoder<T, ContentCodecFormat::kMultipartFormData>
{
    static ContentCodecResult Decode(const ContentView &view, T &out)
    {
        MultiForm form;
        auto result = ContentDecoder<MultiForm, ContentCodecFormat::kMultipartFormData>::Decode(view, form);
        if(!result.ok)
        {
            return result;
        }
        try {

            form.get_to(out);

        } catch(const std::exception &e) {
            return ContentCodecResult::Failed(
                ContentCodecErrorCode::kDecodeFailed,
                std::string("multipart decode failed: ") + e.what());
        }
        return ContentCodecResult::Success();
    }
};


//  TODO xml 序列化/反序列化 暂时不实现
template<typename T>
struct ContentDecoder<T, ContentCodecFormat::kXml>
{
    static ContentCodecResult Decode(const ContentView& view, T& out)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedFormat, "xml decoder not enabled");
    }
};

template<>
struct ContentDecoder<std::string, ContentCodecFormat::kText>
{
    static ContentCodecResult Decode(const ContentView &view, std::string &out)
    {
        return ToHttpContentResult(kit_muduo::TextCodec::Decode(view.toBytesView(), out));
    }
};

template<>
struct ContentDecoder<std::vector<uint8_t>, ContentCodecFormat::kFormUrlEncoded>
{
    static ContentCodecResult Decode(const ContentView& view, std::vector<uint8_t> &out)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedFormat, "form-url-encoded decoder not enabled");
    }
};

template<>
struct ContentDecoder<std::vector<uint8_t>, ContentCodecFormat::kBinary>
{
    // 注意: 二进制数据流不需要转换 直接赋值
    static ContentCodecResult Decode(const ContentView& view, std::vector<uint8_t> &out)
    {
        out.clear();
        if(nullptr != view.data && view.size > 0)
        {
            out.assign(view.data, view.data + view.size);
        }

        return ContentCodecResult::Success();
    }
};




template<typename T>
class ContentDecodePipeline
{
public:
    static ContentCodecResult Decode(const ContentView &view, T& out, std::initializer_list<ContentCodecFormat> allowed_formats)
    {
        if(!view.data && view.size > 0)
        {
            return ContentCodecResult::Failed(ContentCodecErrorCode::kInternalError, "data is null");
        }

        const auto codec_format = ResolveContentCodecFormat(view.meta);

        if(!IsAllowed(codec_format, allowed_formats))
        {
            return ContentCodecResult::Failed(
                ContentCodecErrorCode::kUnsupportedFormat,
                "unsupported content format");
        }

        switch (codec_format)
        {
            case ContentCodecFormat::kJson: return ContentDecoder<T, ContentCodecFormat::kJson>::Decode(view, out);
            case ContentCodecFormat::kMultipartFormData: return ContentDecoder<T, ContentCodecFormat::kMultipartFormData>::Decode(view, out);
            case ContentCodecFormat::kXml: return ContentDecoder<T, ContentCodecFormat::kXml>::Decode(view, out);
            case ContentCodecFormat::kText: return ContentDecoder<T, ContentCodecFormat::kText>::Decode(view, out);
            case ContentCodecFormat::kFormUrlEncoded:
                return ContentCodecResult::Failed(
                    ContentCodecErrorCode::kUnsupportedFormat,
                    "form-url-encoded decoder not enabled");
            case ContentCodecFormat::kBinary: return ContentDecoder<T, ContentCodecFormat::kBinary>::Decode(view, out);
            default:
                return ContentCodecResult::Failed(
                    ContentCodecErrorCode::kUnsupportedFormat,
                    "content format decoder not enabled");
        }
        
    }
private:
    static bool IsAllowed(ContentCodecFormat actual_format, std::initializer_list<ContentCodecFormat> allowed_formats)
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

template<typename T>
ContentCodecResult DecodeMultiPartHelper(const FormPart &part, T &out, std::initializer_list<ContentCodecFormat> allowed_formats)
{
    return ContentDecodePipeline<T>::Decode(part.toContentView(), out, allowed_formats);
}

/**
 * @brief 原样提取Part数据 不进行Type转换检查
 * @param part 
 * @param out 
 * @return ContentCodecResult 
 */
inline ContentCodecResult DecodeMultiPartToRaw(const FormPart &part, std::vector<uint8_t>& out)
{
    out.assign(part.data.begin(), part.data.end());
    return ContentCodecResult::Success();
}

/**
 * @brief 原样提取Part数据 不进行Type转换检查
 * @param part 
 * @param out 
 * @return ContentCodecResult 
 */
inline ContentCodecResult DecodeMultiPartToRaw(const FormPart &part, std::vector<char>& out)
{
    out.assign(part.data.begin(), part.data.end());
    return ContentCodecResult::Success();
}

inline void ThrowIfFailed(const ContentCodecResult& result)
{
    if(!result.ok)
    {
        throw MultiFormException(result.message);
    }
}

/***************TODO 序列化能力预留****************** */

template<typename T, ContentCodecFormat Format>
struct ContentEncoder
{
    static ContentCodecResult Encode(const T&, std::vector<char>&)
    {
        return ContentCodecResult::Failed(ContentCodecErrorCode::kUnsupportedTarget, "encoder unsupported");
    }
};

template<typename T>
struct ContentEncoder<T, ContentCodecFormat::kJson>
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
#endif //__KIT_HTTP_CONTENT_CODEC_H__
