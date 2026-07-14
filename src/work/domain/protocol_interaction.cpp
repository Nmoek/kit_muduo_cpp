/**
 * @file protocol_interaction.cpp
 * @brief 测试协议项交互详情
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-01 23:09:32
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/bytes_codec.h"
#include "base/util.h"
#include "domain/domain_log.h"
#include "domain/type.h"
#include "net/http/http_content.h"
#include "net/net_data_converter.h"
#include "domain/protocol_interaction.h"
#include "domain/http_project_server.h"

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace kit_domain {

namespace {


void AssignTextPrefix(InteractionBody &body, const std::vector<uint8_t> &data, size_t max_bytes)
{
    body.kind = InteractionPayloadKind::kText;
    body.text = Utf8SafePrefix(data.data(), data.size(), max_bytes);
    body.captured_size = body.text.size();
    body.truncated = body.captured_size < data.size();
}

void AssignHexPrefix(InteractionBody &body, const std::vector<uint8_t> &data, size_t max_bytes)
{
    const size_t captured_size = std::min(data.size(), max_bytes);
    std::vector<uint8_t> prefix(data.begin(), data.begin() + captured_size);

    body.kind = InteractionPayloadKind::kBinary;
    body.captured_size = captured_size;
    body.truncated = captured_size < data.size();
    body.text = BytesToHexString(prefix);
}

/**
 * @brief 通过Http的`Content-Type`推导更具体的Body数据类型(Http、类Http协议可用)
 * @param media_type 
 * @return InteractionPayloadKind 
 */
InteractionPayloadKind GuessBinaryKindFromHttpContentType(const std::string &media_type)
{
    const auto meta = kit_muduo::http::ParseHttpContentType(media_type);
    if(meta.media_type.empty())
    {
        return InteractionPayloadKind::kBinary;
    }

    if(meta.parsed_media_type.type == "image")
    {
        return InteractionPayloadKind::kImage;
    }
    if(meta.parsed_media_type.type == "audio")
    {
        return InteractionPayloadKind::kAudio;
    }
    if(meta.parsed_media_type.type == "video")
    {
        return InteractionPayloadKind::kVideo;
    }
    if(meta.media_type == "application/pdf")
    {
        return InteractionPayloadKind::kPdf;
    }
    if(meta.media_type == "multipart/form-data")
    {
        return InteractionPayloadKind::kMultiForm;
    }
    if(meta.media_type == "application/zip"
        || meta.media_type == "application/x-zip-compressed"
        || meta.media_type == "application/gzip"
        || meta.media_type == "application/x-gzip"
        || meta.media_type == "application/x-tar"
        || meta.media_type == "application/x-7z-compressed"
        || meta.media_type == "application/x-rar-compressed")
    {
        return InteractionPayloadKind::kArchive;
    }
    return InteractionPayloadKind::kBinary;
}

}

InteractionBody& InteractionBody::fillJsonBdoy(const std::vector<uint8_t> &data, bool is_utf8_safe, const std::string &utf8_error, const InteractionCaptureOptions & options)
{
    if(!is_utf8_safe)
    {
        INTERAC_F_ERROR("body utf-8 invalid: \n", utf8_error.c_str());
        AssignHexPrefix(*this, data, options.max_hex_bytes);
        this->error_message = "json body utf-8 invalid : " + utf8_error;
        return *this;
    }

    auto result = JsonCodec::Validate({
        .data = data.data(),
        .size = data.size(),
    });

    if(!result.ok)
    {
        INTERAC_F_ERROR("json validate error: %s\n", result.message.c_str());

        AssignTextPrefix(*this, data, options.max_text_bytes);
        error_message = std::move(result.message);
        return *this;
    }

    kind = InteractionPayloadKind::kJson;
    text = Utf8SafePrefix(data.data(), data.size(), options.max_text_bytes);
    captured_size = text.size();
    truncated = captured_size < data.size();


    return *this;
}

InteractionBody& InteractionBody::fillXmlBdoy(const std::vector<uint8_t> &data, bool is_utf8_safe, const std::string &utf8_error, const InteractionCaptureOptions & options)
{
    if(!is_utf8_safe)
    {
        INTERAC_F_ERROR("body utf-8 invalid: %s\n", utf8_error.c_str());

        AssignHexPrefix(*this, data, options.max_hex_bytes);
        this->error_message = "xml body utf-8 invalid : " + utf8_error;
        return *this;
    }

    auto result = XmlCodec::Validate({
        .data = data.data(),
        .size = data.size(),
    });

    if(!result.ok)
    {
        INTERAC_F_ERROR("xml validate error: %s\n", result.message.c_str());

        AssignTextPrefix(*this, data, options.max_text_bytes);
        error_message = std::move(result.message);
        return *this;
    }

    kind = InteractionPayloadKind::kXml;
    text = Utf8SafePrefix(data.data(), data.size(), options.max_text_bytes);
    captured_size = text.size();
    truncated = captured_size < data.size();

    return *this;
}

InteractionBody& InteractionBody::fillTextBdoy(const std::vector<uint8_t> &data, bool is_utf8_safe, const std::string &utf8_error, const InteractionCaptureOptions &options)
{
    if(!is_utf8_safe)
    {
        INTERAC_F_ERROR("body utf-8 invalid: %s\n", utf8_error.c_str());

        AssignHexPrefix(*this, data, options.max_hex_bytes);
        this->error_message = "text body utf-8 invalid : " + utf8_error;
        return *this;
    }

    kind = InteractionPayloadKind::kText;
    text = Utf8SafePrefix(data.data(), data.size(), options.max_text_bytes);
    captured_size = text.size();
    truncated = captured_size < data.size();

    return *this;
}

InteractionBody& InteractionBody::fillBinaryBdoy(const std::vector<uint8_t> &data, const InteractionPayloadHint &hint, const ProtocolSide &side, bool is_utf8_safe, const std::string &utf8_error, const std::string &flag, const InteractionCaptureOptions & options, std::vector<BinarySidecar> &sidecars)
{
    // 默认按二进制处理
    this->kind = InteractionPayloadKind::kBinary;
    if(ProtocolType::kHttp == hint.protocol_type
        || ProtocolType::kHttps == hint.protocol_type)
    {
        this->kind = GuessBinaryKindFromHttpContentType(hint.media_type);
    }

    this->size = data.size();
    this->sha1 = Sha1BytesBase64Helper(data);

    // 注意 这里只有CustomTcp使用
    if(hint.prefer_hex_text_for_binary)
    {
        this->captured_size = static_cast<size_t>(std::min(data.size(), options.max_hex_bytes));
        this->truncated = captured_size < data.size();

        this->text = BytesToHexString(std::vector<uint8_t>(data.begin(), data.begin() + captured_size));
    }
    else
    {
    
        this->captured_size =  std::min(data.size(), options.max_binary_attachment_bytes);
        this->truncated = this->captured_size < data.size();

        InteractionAttachmentRef ref{
            .attachment_id = flag + ":" + sha1.substr(0, 16),
            .side = ProtocolSideToString(side),
            .flag = flag,
            .kind = this->kind, 
            .size = data.size(),
            .captured_size = this->captured_size,
            .truncated = this->truncated,
            .binary_available = !this->truncated && this->captured_size == data.size(),
            .sha1 = sha1,
        };

        attachments.push_back(ref);

        // 二进制数据完整未截断 追加到二进制帧缓存
        if(ref.binary_available)
        {
            sidecars.push_back(BinarySidecar{
                .attachment_ref = ref,
                .bytes = std::make_shared<const std::vector<uint8_t>>(data),
            });
        }
    
    }

    return *this;
}

InteractionBody InteractionBody::BuildFromBytes(const std::vector<uint8_t> &data, const InteractionPayloadHint &hint, const ProtocolSide &side, const InteractionCaptureOptions &options, std::vector<BinarySidecar> &sidecars)
{
    InteractionBody body;
    body.size = data.size();
    body.expect_kind = ProtocolBodyTypeToInterKind(hint.expect_body_type);
    body.sha1 = Sha1BytesBase64Helper(data);

    if(data.empty())
    {
        body.kind = InteractionPayloadKind::kEmpty;
        body.size = body.captured_size = 0;
        return body;
    }
    
    const std::string& flag = ProtocolSideToString(side) + "." + "body";

    std::string utf8_error;
    bool is_utf8_safe = IsUtf8Safe(data.data(), data.size(), utf8_error);
    
    switch (hint.expect_body_type) 
    {
        case ProtocolBodyType::kJson: return body.fillJsonBdoy(data, is_utf8_safe, utf8_error, options);
        case ProtocolBodyType::kXml: return body.fillXmlBdoy(data, is_utf8_safe, utf8_error, options);
        case ProtocolBodyType::kText: return body.fillTextBdoy(data, is_utf8_safe, utf8_error, options);
        default:
            return body.fillBinaryBdoy(data, hint, side, is_utf8_safe, utf8_error, flag, options, sidecars);
    }
}

 InteractionRawPacket InteractionRawPacket::BuildRawPacketFromBytes(const std::vector<uint8_t> &data,
    const ProtocolSide &side,
    const InteractionCaptureOptions &options,
    std::vector<BinarySidecar> &sidecars)
{
    InteractionRawPacket raw;
    raw.kind = InteractionPayloadKind::kBinary;
    raw.size = data.size();
    raw.captured_size = std::min(options.max_hex_bytes, data.size());
    raw.sha1 = Sha1BytesBase64Helper(data);
    raw.truncated = raw.captured_size < data.size();

    const std::string &flag = ProtocolSideToString(side) + "." + "raw_packet";

    raw.raw_hex = BytesToHexString(std::vector<uint8_t>(data.begin(),  data.begin() + raw.captured_size));

    return raw;
}


}
