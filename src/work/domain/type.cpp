/**
 * @file type.cpp
 * @brief 外部-内部类型转换
 * @author ljk5
 * @version 1.0
 * @date 2025-08-25 15:23:28
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/type.h"
#include "domain/protocol_interaction.h"

namespace kit_domain {




ProtocolType ProtocolTypeFromString(const std::string &type)
{
    if("HTTP" == type) return ProtocolType::kHttp;
    else if("TCP" == type) return ProtocolType::kCustomTcp;
    else if("HTTPS" == type) return ProtocolType::kHttps;
    
    return ProtocolType::kUnknown;
}

std::string ProtocolTypeToString(ProtocolType type)
{
    if(ProtocolType::kHttp == type) return "HTTP";
    else if(ProtocolType::kCustomTcp == type) return "TCP";
    else if(ProtocolType::kHttps == type) return "HTTPS";
    
    return "";
}

ProtocolBodyType ProtocolBodyTypeFromString(const std::string &type)
{
    if("json" == type) return ProtocolBodyType::kJson;
    else if("xml" == type) return ProtocolBodyType::kXml;
    else if("text" == type) return ProtocolBodyType::kText;
    else if("binary" == type) return ProtocolBodyType::kBinary;
    return ProtocolBodyType::kUnknown;
}

std::string ProtocolBodyTypeToString(ProtocolBodyType type)
{
    if(ProtocolBodyType::kJson == type) return "json";
    else if(ProtocolBodyType::kXml == type) return "xml";
    else if(ProtocolBodyType::kText == type) return "text";
    else if(ProtocolBodyType::kBinary == type) return "binary";

    return "";
}

InteractionPayloadKind ProtocolBodyTypeToInterKind(ProtocolBodyType type)
{
    switch (type) 
    {
        case ProtocolBodyType::kJson: return InteractionPayloadKind::kJson;
        case ProtocolBodyType::kText: return InteractionPayloadKind::kText;
        case ProtocolBodyType::kXml: return InteractionPayloadKind::kXml;
        case ProtocolBodyType::kMultiForm: return InteractionPayloadKind::kMultiForm;
        case ProtocolBodyType::kBinary: return InteractionPayloadKind::kBinary;
        // 注意：这里其他暂不支持配置的类型全部认为是二进制
        // TODO 传入unknown 时也认为是二进制 暂时搁置
        default:
            return InteractionPayloadKind::kBinary;
    }
}

kit_muduo::http::ContentCodecFormat ProtocolBodyTypeToContentCodecFormat(ProtocolBodyType type)
{
    using kit_muduo::http::ContentCodecFormat;

    switch(type)
    {
        case ProtocolBodyType::kJson:
            return ContentCodecFormat::kJson;
        case ProtocolBodyType::kXml:
            return ContentCodecFormat::kXml;
        case ProtocolBodyType::kText:
            return ContentCodecFormat::kText;
        case ProtocolBodyType::kBinary:
            return ContentCodecFormat::kBinary;
        default:
            return ContentCodecFormat::kNone;
    }
}

kit_muduo::http::ContentMeta ProtocolBodyTypeToHttpContentMeta(ProtocolBodyType type)
{
    using kit_muduo::http::KnownMediaType;
    using kit_muduo::http::MakeContentMeta;

    switch(type)
    {
        case ProtocolBodyType::kJson:
            return MakeContentMeta(KnownMediaType::kApplicationJson);
        case ProtocolBodyType::kXml:
            return MakeContentMeta(KnownMediaType::kApplicationXml);
        case ProtocolBodyType::kText:
            return MakeContentMeta(KnownMediaType::kTextPlain);
        case ProtocolBodyType::kBinary:
            return MakeContentMeta(KnownMediaType::kApplicationOctetStream);
        default:
            return MakeContentMeta(KnownMediaType::kUnknown);
    }
}


std::string ProtocolSideToString(ProtocolSide side)
{
    if(ProtocolSide::kRequest == side)
    {
        return "request";
    }
    else if(ProtocolSide::kResponse == side)
    {
        return "response";
    }
    return "";
}

} // namespace kit_domain
