
/**
 * @file type.h
 * @brief 外部-内部 类型转换
 * @author ljk5
 * @version 1.0
 * @date 2025-08-25 15:23:28
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_TYPE_H__
#define __KIT_DOMAIN_TYPE_H__

#include "nlohmann/json.hpp"

#include <string>
#include <vector>

namespace kit_muduo {
namespace http {

struct ContentType;

}
}


namespace kit_domain {



enum ProjectMode {
    ServerMode = 1,  //服务器模式
    ClientMode = 2,  //客户端模式
};

enum ProjectStatus {
    OFF_STATUS = 0,  //关闭
    ON_STATUS = 1,  //开启
};

enum class ProtocolType {
    UNKNOWN_PROTOCOL    = 0,    //未知协议
    HTTP_PROTOCOL       = 1,    //HTTP 协议
    CUSTOM_TCP_PROTOCOL = 2,    //自定义TCP协议
    HTTPS_PROTOCOL      = 3,    //HTTPs 协议
    MAX,
};

///  @brief 业务上Body类型
enum class ProtocolBodyType {
    UNKNOWN_BODY_TYPE   = 0,     //未知格式(没有设置)
    JSON_BODY_TYPE      = 1,    // json格式
    XML_BODY_TYPE       = 2,    // xml格式
    BINARY_BODY_TYPE    = 3,    // TCP 二进制数据流
    MAX,
};

enum class ProtocolStatus {
    UNKNOWN     = 0,
    ACTIVE      = 1,
    INACTIVE    = 2,
};

/**
 * @brief 协议校验模式
 */
enum class VerifyMode {
    UNKNOWN     = 0,
    SAMPLE      = 1, // 简化校验
    STRICT      = 2, // 严格校验
};

enum class CustomTcpPatternType {
    STANDARD          = 0, // 标准格式
    BODY_LENGTH_DEP   = 1, // Body长度依赖
    TOTAL_LENGTH_DEP  = 2, // 总长度依赖
    NO_LENGTH_DEP     = 3, // 无长度长度依赖(需要用户配置)
};


ProtocolType ProtocolTypeFromString(const std::string &type);

std::string ProtocolTypeToString(ProtocolType type);

ProtocolBodyType ProtocolBodyTypeFromString(const std::string &type);
std::string ProtocolBodyTypeToString(ProtocolBodyType type);

kit_muduo::http::ContentType ProtocolBodyTypeToContentType(ProtocolBodyType type);


}
#endif // __KIT_DOMAIN_PROTOCOL_H__