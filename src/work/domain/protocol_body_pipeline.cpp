/**
 * @file protocol_body_pipeline.cpp
 * @brief 协议项Body校验器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-16 16:46:39
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/domain_log.h"
#include "domain/project_server.h"
#include "domain/type.h"
#include "nlohmann/json.hpp"
#include "domain/protocol_body_pipeline.h"
#include "domain/protocol.h"
#include "pugixml/pugixml.hpp"

#include <cstdint>
#include <exception>

namespace kit_domain {

namespace {

constexpr unsigned char kAsciiControlEnd = 0x1F;
constexpr unsigned char kAsciiMax = 0x7F;
constexpr unsigned char kHorizontalTab = '\t';
constexpr unsigned char kLineFeed = '\n';
constexpr unsigned char kCarriageReturn = '\r';

// UTF-8 后续字节固定是 10xxxxxx，也就是 0x80-0xBF。
constexpr unsigned char kUtf8ContinuationMin = 0x80;
constexpr unsigned char kUtf8ContinuationMax = 0xBF;

// UTF-8 按序列长度划分的合法首字节范围。
// 0xC0 和 0xC1 被故意排除，因为它们只能构造 ASCII 码点的 2 字节超长编码。
constexpr unsigned char kUtf8TwoByteLeadMin = 0xC2;
constexpr unsigned char kUtf8TwoByteLeadMax = 0xDF;
constexpr unsigned char kUtf8ThreeByteLeadMin = 0xE0;
constexpr unsigned char kUtf8ThreeByteLeadMax = 0xEF;
constexpr unsigned char kUtf8FourByteLeadMin = 0xF0;
constexpr unsigned char kUtf8FourByteLeadMax = 0xF4;

// 掩码用于提取 UTF-8 字节中真正承载码点的有效位。
// 后续字节贡献 6 个有效位：10xxxxxx -> xxxxxx。
constexpr unsigned char kUtf8TwoByteLeadMask = 0x1F;
constexpr unsigned char kUtf8ThreeByteLeadMask = 0x0F;
constexpr unsigned char kUtf8FourByteLeadMask = 0x07;
constexpr unsigned char kUtf8ContinuationMask = 0x3F;
constexpr uint32_t kUtf8ContinuationPayloadBits = 6;

// 不同长度 UTF-8 序列能表示的最小 Unicode 码点。
// 解码后的码点如果小于对应阈值，说明它使用了超长编码，必须拒绝。
constexpr uint32_t kUtf8TwoByteMinCodePoint = 0x80;
constexpr uint32_t kUtf8ThreeByteMinCodePoint = 0x800;
constexpr uint32_t kUtf8FourByteMinCodePoint = 0x10000;

// UTF-8 只能表示 Unicode scalar value。
// surrogate 码点是 UTF-16 的保留区间，不是 UTF-8 中的合法 Unicode scalar value。
constexpr uint32_t kUnicodeSurrogateMin = 0xD800;
constexpr uint32_t kUnicodeSurrogateMax = 0xDFFF;
constexpr uint32_t kUnicodeMaxCodePoint = 0x10FFFF;

inline bool IsBodyTypeValid(ProtocolBodyType body_type)
{
    return body_type > ProtocolBodyType::kUnknown && body_type < ProtocolBodyType::kMax;
}

inline bool IsUtf8Continuation(unsigned char byte)
{
    return byte >= kUtf8ContinuationMin && byte <= kUtf8ContinuationMax;
}

}

ProtocolBodyCheckResult ProtocolBodyPipeline::CheckBody(const ProtocolBodySpec &spec)
{
    if(!IsBodyTypeValid(spec.body_type))
    {
        RUNTIME_F_ERROR("protocol body type invalid! %d\n", static_cast<int32_t>(spec.body_type));
        return ProtocolBodyCheckResult::Failed("protocol body type invalid");
    }

    const auto policy = getPolicy(spec.body_type);
    if(!policy)
    {
        RUNTIME_F_ERROR("unsupported protocol body policy! %d\n", static_cast<int32_t>(spec.body_type));
        return ProtocolBodyCheckResult::Failed("unsupported protocol body policy");
    }

    return policy->check(spec);
}

ProtocolBodyCheckResult ProtocolBodyPipeline::CheckFullBody(const ProtocolFullBodySpec &spec)
{
    auto req_result = CheckBody(spec.toReqSpec());
    if(!req_result.ok)
    {
        RUNTIME_F_ERROR("request body check error: %s\n", req_result.message.c_str());
        return ProtocolBodyCheckResult::Failed("request body invalid: " + req_result.message);
    }

    auto resp_result = CheckBody(spec.toRespSpec());
    if(!resp_result.ok)
    {
        RUNTIME_F_ERROR("response body check error: %s\n", resp_result.message.c_str());
        return ProtocolBodyCheckResult::Failed("response body invalid: " + resp_result.message);
    }

    return ProtocolBodyCheckResult::Success();
}

ProtocolBodyCheckResult ProtocolBodyPipeline::CheckFullProtocol(const Protocol &p)
{
    return CheckFullBody({
        .req_body_type = p.m_reqBodyType,
        .req_body_data = p.m_reqBodyData,
        .resp_body_type = p.m_respBodyType,
        .resp_body_data = p.m_respBodyData,
    });
}

const ProtocolBodyPolicy* ProtocolBodyPipeline::getPolicy(ProtocolBodyType body_type)
{
    if(ProtocolBodyType::kJson == body_type)
    {
        static JsonBodyPolicy p;
        return &p;
    }
    if(ProtocolBodyType::kXml == body_type)
    {
        static XmlBodyPolicy p;
        return &p;
    }
    if(ProtocolBodyType::kText == body_type)
    {
        static TextBodyPolicy p;
        return &p;
    }
    if(ProtocolBodyType::kBinary == body_type)
    {
        static BinaryBodyPolicy p;
        return &p;
    }
    return nullptr;
}


ProtocolBodyCheckResult ProtocolBodyPolicy::check(const ProtocolBodySpec &spec) const
{
    // 注意: 当前策略协议项可以配置空的Body
    if(spec.body_data.empty())
    {
        return ProtocolBodyCheckResult::Success("body allow empty");
    }
    return checkNonEmptyBody(spec);
}

ProtocolBodyCheckResult JsonBodyPolicy::checkNonEmptyBody(const ProtocolBodySpec &spec) const
{
    try {

        if(!nlohmann::json::accept(spec.body_data))
        {
            RUNTIME_F_ERROR("json body invalid!\n");
            return ProtocolBodyCheckResult::Failed("json body invalid");
        }

    } catch (const std::exception &e) {
        return ProtocolBodyCheckResult::Failed("json body invalid");
    }
    return ProtocolBodyCheckResult::Success();
}

ProtocolBodyCheckResult XmlBodyPolicy::checkNonEmptyBody(const ProtocolBodySpec &spec) const
{

    try {
        pugi::xml_document doc;
        auto xml_result = doc.load_buffer(spec.body_data.data(), spec.body_data.size());
        if(pugi::xml_parse_status::status_ok!= xml_result.status)
        {
            return ProtocolBodyCheckResult::Failed(std::string("xml body invalid: ") + xml_result.description());
        }
        int32_t root_count = 0;
        for(auto node = doc.first_child(); node; node = node.next_sibling())
        {
            if(node.type() == pugi::node_element)
            {
                ++root_count;
            }
        }

        if(root_count != 1)
        {
            return ProtocolBodyCheckResult::Failed(
                "xml body invalid: document must only have one 'root' element");
        }
    } catch (const std::exception &e) {
        return ProtocolBodyCheckResult::Failed(std::string("xml body exception: ") + e.what());
    }
    return ProtocolBodyCheckResult::Success();
}

ProtocolBodyCheckResult TextBodyPolicy::checkNonEmptyBody(const ProtocolBodySpec &spec) const
{
    const auto &data = spec.body_data;
    size_t i = 0;
    while(i < data.size())
    {
        const auto first = static_cast<unsigned char>(data[i]);

        // text body 是普通 UTF-8 文本，不是任意二进制数据。
        // NUL 经常被当成 C 字符串终止符，必须显式拒绝。
        if(first == '\0')
        {
            return ProtocolBodyCheckResult::Failed("text body contains nul byte");
        }

        // C0 控制字符不是可打印文本。
        // 纯文本中只保留用户自然会使用的三个空白控制符：TAB、LF、CR。
        if(first <= kAsciiControlEnd)
        {
            if(first == kHorizontalTab || first == kLineFeed || first == kCarriageReturn)
            {
                ++i;
                continue;
            }
            return ProtocolBodyCheckResult::Failed("text body contains invalid control character");
        }

        // 0x20-0x7F 是单字节 ASCII 码点。
        if(first <= kAsciiMax)
        {
            ++i;
            continue;
        }

        size_t need = 0;
        uint32_t code_point = 0;
        uint32_t min_code_point = 0;
        if(first >= kUtf8TwoByteLeadMin && first <= kUtf8TwoByteLeadMax)
        {
            need = 2;
            code_point = first & kUtf8TwoByteLeadMask;
            min_code_point = kUtf8TwoByteMinCodePoint;
        }
        else if(first >= kUtf8ThreeByteLeadMin && first <= kUtf8ThreeByteLeadMax)
        {
            need = 3;
            code_point = first & kUtf8ThreeByteLeadMask;
            min_code_point = kUtf8ThreeByteMinCodePoint;
        }
        else if(first >= kUtf8FourByteLeadMin && first <= kUtf8FourByteLeadMax)
        {
            need = 4;
            code_point = first & kUtf8FourByteLeadMask;
            min_code_point = kUtf8FourByteMinCodePoint;
        }
        else
        {
            // 这里会拦住孤立的后续字节、已经不合法的 UTF-8 5/6 字节前缀，
            // 以及 0xC0/0xC1 这类超长编码前缀。
            return ProtocolBodyCheckResult::Failed("text body must be valid utf-8 text");
        }

        // 当前 UTF-8 序列声明需要更多后续字节，但 body 剩余长度不够。
        if(i + need > data.size())
        {
            return ProtocolBodyCheckResult::Failed("text body must be valid utf-8 text");
        }

        for(size_t j = 1; j < need; ++j)
        {
            const auto next = static_cast<unsigned char>(data[i + j]);
            if(!IsUtf8Continuation(next))
            {
                return ProtocolBodyCheckResult::Failed("text body must be valid utf-8 text");
            }
            code_point = (code_point << kUtf8ContinuationPayloadBits) | (next & kUtf8ContinuationMask);
        }

        if(code_point < min_code_point
            || (code_point >= kUnicodeSurrogateMin && code_point <= kUnicodeSurrogateMax)
            || code_point > kUnicodeMaxCodePoint)
        {
            return ProtocolBodyCheckResult::Failed("text body must be valid utf-8 text");
        }

        i += need;
    }

    return ProtocolBodyCheckResult::Success();
}

ProtocolBodyCheckResult BinaryBodyPolicy::checkNonEmptyBody(const ProtocolBodySpec &spec) const
{
    (void)spec;
    return ProtocolBodyCheckResult::Success();
}

}
