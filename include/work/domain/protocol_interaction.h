/**
 * @file protocol_interaction.h
 * @brief 测试协议项交互详情
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-01 16:12:23
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_PROTOCOL_INTERACTION_H__
#define __KIT_PROTOCOL_INTERACTION_H__


#include "domain/type.h"
#include "nlohmann/json.hpp"

#include <optional>

namespace kit_domain {

enum class HttpObserveResult;


enum class InteractionPayloadKind
{
    kUnknown,
    kEmpty,
    kJson,
    kXml,
    kText,
    kMultiForm,
    kImage,
    kAudio,
    kVideo,
    kPdf,
    kArchive,
    kBinary,
};
NLOHMANN_JSON_SERIALIZE_ENUM(InteractionPayloadKind, {
    {InteractionPayloadKind::kUnknown, "unknown"},
    {InteractionPayloadKind::kEmpty, "empty"},
    {InteractionPayloadKind::kJson, "json"},
    {InteractionPayloadKind::kXml, "xml"},
    {InteractionPayloadKind::kText, "text"},
    {InteractionPayloadKind::kMultiForm, "multiform"},
    {InteractionPayloadKind::kImage, "image"},
    {InteractionPayloadKind::kAudio, "audio"},
    {InteractionPayloadKind::kVideo, "video"},
    {InteractionPayloadKind::kPdf, "pdf"}, 
    {InteractionPayloadKind::kArchive, "archive"}, 
    {InteractionPayloadKind::kBinary, "binary"},
})

enum class InteractionScope
{
    kUnknown = 0,
    kProtocol,
    kProject,
};
NLOHMANN_JSON_SERIALIZE_ENUM(InteractionScope, {
    {InteractionScope::kUnknown, "unknown"},
    {InteractionScope::kProtocol, "protocol"},
    {InteractionScope::kProject, "project"},
})

enum class InteractionResult
{
    // 大类Matched 小类错误 scope=protocol
    kMatched,
    kRequestMismatch,
    kProtocolNotFound,
    kSerializeError,

    // 大类非Matched scope=project
    kParseError,
    kRouteNotFound,
    kMethodNotAllowed,
    kInternalError,
};
NLOHMANN_JSON_SERIALIZE_ENUM(InteractionResult, {
    {InteractionResult::kMatched, "matched"},
    {InteractionResult::kRequestMismatch, "request_mismatch"},
    {InteractionResult::kProtocolNotFound, "protocol_not_found"},
    {InteractionResult::kSerializeError, "serialize_error"},
    {InteractionResult::kParseError, "parse_error"},
    {InteractionResult::kRouteNotFound, "route_not_found"},
    {InteractionResult::kMethodNotAllowed, "method_not_allowed"},
    {InteractionResult::kInternalError, "internal_error"},
})


/**
 * @brief 附件设计主要为二进制类数据设计: 图片、音视频、文件等，普通文本数据text、json、xml等可以考虑不使用
 */
struct InteractionAttachmentRef
{
    std::string attachment_id;
    std::string side;
    std::string flag;  // request.body / response.body / request.raw_packet
    InteractionPayloadKind kind{InteractionPayloadKind::kBinary};
    uint64_t size{0};
    uint64_t captured_size{0};
    bool truncated{false};
    bool binary_available{false};
    std::string sha1;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(InteractionAttachmentRef, attachment_id, side, flag, kind, size, captured_size, truncated, binary_available, sha1)
};

struct BinarySidecar
{
    InteractionAttachmentRef attachment_ref;
    std::shared_ptr<const std::vector<uint8_t>> bytes{nullptr};
};

struct InteractionPayloadHint
{

    /// @brief 行时协议类型。HTTP 可以结合 content_type判断image/pdf/zip (CustomTcp默认把 body 当 binary 十六进制展示。)
    ProtocolType protocol_type{ProtocolType::kUnknown};

    /// @brief 协议项配置里的期望 body 类型，最终写入 body.expect_kind。
    ProtocolBodyType expect_body_type{ProtocolBodyType::kNone};

    // HTTP Content-Type 去掉参数后的 media type。
    // 仅用于分类，不在 InteractionBody 里重复输出；原始 header 仍只在 head_text 中展示。
    std::string media_type;

    // true 时，binary body 优先生成 body.text 十六进制前缀。
    // CustomTcp request/response 传 true；HTTP 图片、PDF、zip 等一般传 false。
    bool prefer_hex_text_for_binary{false};
};

struct InteractionCaptureOptions
{
    static constexpr size_t kDefaultMaxTextBytes = 64*1024;//64K
    static constexpr size_t kDefaultMaxAttachmentBytes = 10*1024*1024;//10M

    size_t max_text_bytes{kDefaultMaxTextBytes};
    size_t max_hex_bytes{kDefaultMaxTextBytes};
    size_t max_binary_attachment_bytes{kDefaultMaxAttachmentBytes};
};

/**
 * @brief 不同测试协议项的Body数据
 */
struct InteractionBody
{
    InteractionPayloadKind kind{InteractionPayloadKind::kEmpty};
    InteractionPayloadKind expect_kind{InteractionPayloadKind::kEmpty};
    uint64_t size{0};
    uint64_t captured_size{0};
    bool truncated{false};
    std::string sha1;
    std::string text;
    std::string error_message;
    std::vector<InteractionAttachmentRef> attachments;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(InteractionBody, kind, expect_kind, size, captured_size, truncated, sha1, text, error_message, attachments)

    InteractionBody& fillJsonBdoy(const std::vector<uint8_t> &data,
        bool is_utf8_safe,
        const std::string &utf8_error,
        const InteractionCaptureOptions & options);

    InteractionBody& fillXmlBdoy(const std::vector<uint8_t> &data,
        bool is_utf8_safe,
        const std::string &utf8_error,
        const InteractionCaptureOptions & options);

    InteractionBody& fillTextBdoy(const std::vector<uint8_t> &data,
        bool is_utf8_safe,
        const std::string &utf8_error,
        const InteractionCaptureOptions & options);

    // TODO 处理multipart-form-data
    InteractionBody& fillMultiFormBdoy(const std::vector<uint8_t> &data, 
        bool is_utf8_safe, 
        const std::string &utf8_error, 
        const InteractionCaptureOptions & options);


    InteractionBody& fillBinaryBdoy(const std::vector<uint8_t> &data, 
        const InteractionPayloadHint &hint, 
        const ProtocolSide &side, 
        bool is_utf8_safe, 
        const std::string &utf8_error, 
        const std::string &flag, 
        const InteractionCaptureOptions & options, 
        std::vector<BinarySidecar> &sidecars);

    static InteractionBody BuildFromBytes(const std::vector<uint8_t> &data, 
        const InteractionPayloadHint &hint, 
        const ProtocolSide &side, const 
        InteractionCaptureOptions &options, 
        std::vector<BinarySidecar> &sidecars);


};

/**
 * @brief 捕获数据无法正常拆包时 按照raw bytes处理推送
 */
struct InteractionRawPacket
{
    InteractionPayloadKind kind{InteractionPayloadKind::kBinary};
    uint64_t size{0};
    uint64_t captured_size{0};
    bool truncated{false};
    std::string sha1;
    std::string raw_hex;
    std::vector<InteractionAttachmentRef> attachments;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(InteractionRawPacket, kind, size, captured_size, truncated, sha1, raw_hex, attachments)

    static InteractionRawPacket BuildRawPacketFromBytes(const std::vector<uint8_t> &data,
    const ProtocolSide &side,
    const InteractionCaptureOptions &options,
    std::vector<BinarySidecar> &sidecars);
};

struct InteractionSide
{
    /// @brief 不同测试协议项的关键元数据信息
    nlohmann::json meta = nlohmann::json::object();
    std::string head_text;
    InteractionBody body;
    std::optional<InteractionRawPacket> raw_packet;

    friend void to_json(nlohmann::json& j, const InteractionSide& obj) 
    { 
        j["meta"] = obj.meta;
        j["head_text"] = obj.head_text;
        j["body"] = obj.body;
        if(obj.raw_packet.has_value())
        {
            j["raw_packet"] = obj.raw_packet.value();
        }
    } 

    friend void from_json(const nlohmann::json& j, InteractionSide& obj)
    {
        j.at("meta").get_to(obj.meta);
        j.at("head_text").get_to(obj.head_text);
        j.at("body").get_to(obj.body);
        if(j.find("raw_packet") == j.end())
        {
            obj.raw_packet = std::nullopt;
        }
        else
        {
            obj.raw_packet = j.at("raw_packet");
        }
    }
};


struct ProtocolInteractionRecord
{
    uint64_t seq{0};
    InteractionScope scope{InteractionScope::kUnknown};
    int64_t project_id{0};
    int64_t protocol_id{0};
    ProtocolType protocol_type{ProtocolType::kUnknown};
    int64_t time_ms{0};
    std::string peer_addr;
    InteractionResult result{InteractionResult::kMatched};
    std::string error_message;
    InteractionSide request;
    InteractionSide response;
    /// @brief 注意: 不放入json序列化 仅作为逻辑结构托管
    std::vector<BinarySidecar> binary_sidecars;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProtocolInteractionRecord, seq, scope, project_id, protocol_id, protocol_type, time_ms, peer_addr, result, error_message, request, response)

    static InteractionResult ToInteractionResult(HttpObserveResult obs_result);
};


}

#endif //__KIT_PROTOCOL_INTERACTION_H__