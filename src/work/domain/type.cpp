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

ContentType ProtocolBodyTypeToContentType(ProtocolBodyType type)
{
    if(ProtocolBodyType::kJson == type) return ContentType(ContentType::kJsonType);
    if(ProtocolBodyType::kXml == type) return ContentType(ContentType::kXmlType);

    // 默认给个文本类型
    return ContentType(ContentType::kPlainType);
}

} // namespace kit_domain
    


