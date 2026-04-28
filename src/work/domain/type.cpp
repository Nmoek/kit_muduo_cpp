/**
 * @file type.cpp
 * @brief 外部-内部类型转换
 * @author ljk5
 * @version 1.0
 * @date 2025-08-25 15:23:28
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/type.h"
#include "net/http/http_util.h"

using namespace kit_muduo::http;

namespace kit_domain {


ProtocolType ProtocolTypeFromString(const std::string &type)
{
    if("HTTP" == type) return ProtocolType::HTTP_PROTOCOL;
    else if("TCP" == type) return ProtocolType::CUSTOM_TCP_PROTOCOL;
    else if("HTTPS" == type) return ProtocolType::HTTPS_PROTOCOL;
    
    return ProtocolType::UNKNOWN_PROTOCOL;
}

std::string ProtocolTypeToString(ProtocolType type)
{
    if(ProtocolType::HTTP_PROTOCOL == type) return "HTTP";
    else if(ProtocolType::CUSTOM_TCP_PROTOCOL == type) return "TCP";
    else if(ProtocolType::HTTPS_PROTOCOL == type) return "HTTPS";
    
    return "";
}

ProtocolBodyType ProtocolBodyTypeFromString(const std::string &type)
{
    if("json" == type) return ProtocolBodyType::JSON_BODY_TYPE;
    else if("xml" == type) return ProtocolBodyType::XML_BODY_TYPE;
    else if("binary" == type) return ProtocolBodyType::BINARY_BODY_TYPE;
    return ProtocolBodyType::UNKNOWN_BODY_TYPE;
}

std::string ProtocolBodyTypeToString(ProtocolBodyType type)
{
    if(ProtocolBodyType::JSON_BODY_TYPE == type) return "json";
    else if(ProtocolBodyType::XML_BODY_TYPE == type) return "xml";
    else if(ProtocolBodyType::BINARY_BODY_TYPE == type) return "binary";

    return "";
}

ContentType ProtocolBodyTypeToContentType(ProtocolBodyType type)
{
    if(ProtocolBodyType::JSON_BODY_TYPE == type) return ContentType(ContentType::kJsonType);
    if(ProtocolBodyType::XML_BODY_TYPE == type) return ContentType(ContentType::kXmlType);

    return ContentType(ContentType::kUnknowType);
}

} // namespace kit_domain
    


