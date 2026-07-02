/**
 * @file protocol_body_pipeline.cpp
 * @brief 协议项Body校验器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-16 16:46:39
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/util.h"
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
constexpr unsigned char kHorizontalTab = '\t';
constexpr unsigned char kLineFeed = '\n';
constexpr unsigned char kCarriageReturn = '\r';

inline bool IsBodyTypeValid(ProtocolBodyType body_type)
{
    return body_type > ProtocolBodyType::kUnknown && body_type < ProtocolBodyType::kMax;
}

ProtocolBodyCheckResult CheckTextAsciiControl(const std::vector<char> &data)
{
    for(const char item : data)
    {
        const auto byte = static_cast<unsigned char>(item);

        // text body 是普通 UTF-8 文本，不是任意二进制数据。
        // NUL 经常被当成 C 字符串终止符，必须显式拒绝。
        if(byte == '\0')
        {
            return ProtocolBodyCheckResult::Failed("text body contains nul byte");
        }

        // C0 控制字符不是可打印文本。
        // 纯文本中只保留用户自然会使用的三个空白控制符：TAB、LF、CR。
        if(byte <= kAsciiControlEnd)
        {
            if(byte == kHorizontalTab || byte == kLineFeed || byte == kCarriageReturn)
            {
                continue;
            }
            return ProtocolBodyCheckResult::Failed("text body contains invalid control character");
        }
    }
    return ProtocolBodyCheckResult::Success();
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
    const auto& data = spec.body_data;
    auto result = CheckTextAsciiControl(data);
    if(!result.ok)
    {
        RUNTIME_F_ERROR("body ascii control invalid: %s \n", result.message.c_str());
        return result;
    }

    std::string utf8_error;
    if(!kit_muduo::IsUtf8Safe(data.data(), data.size(), utf8_error))
    {
        RUNTIME_F_ERROR("body utf8 invalid: %s \n", utf8_error.c_str());
        return ProtocolBodyCheckResult::Failed("text body must be valid utf-8 text: " + utf8_error);
    }

    return ProtocolBodyCheckResult::Success();
}

ProtocolBodyCheckResult BinaryBodyPolicy::checkNonEmptyBody(const ProtocolBodySpec &spec) const
{
    (void)spec;
    return ProtocolBodyCheckResult::Success();
}

}
