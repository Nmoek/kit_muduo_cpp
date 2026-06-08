
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

inline bool CheckProjectMode(ProjectMode mode)
{
    return mode >= ProjectMode::ServerMode && mode <= ProjectMode::ClientMode;
}


enum class ProjectStatus {
    kInvalid = 0,  //无效
    kValid = 1,  //有效
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProjectStatus,{
    {ProjectStatus::kInvalid, 0},
    {ProjectStatus::kValid,   1},
})

enum class ProjectRuntimeState {
    kStopped = 0,  // 服务未启动
    kRunning = 1,  // 服务已启动
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProjectRuntimeState, {
    {ProjectRuntimeState::kStopped, 0},
    {ProjectRuntimeState::kRunning, 1},
})

enum class ProtocolType {
    kUnknown    = 0,    //未知协议
    kHttp       = 1,    //HTTP 协议
    kCustomTcp  = 2,    //自定义TCP协议
    kHttps      = 3,    //HTTPs 协议
    kMax,
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolType, {
    {ProtocolType::kUnknown,    0},
    {ProtocolType::kHttp,       1},
    {ProtocolType::kCustomTcp,  2},
    {ProtocolType::kHttps,      3},
})
inline bool CheckProtocolType(ProtocolType type)
{
    return type > ProtocolType::kUnknown && type < ProtocolType::kMax;
}


enum class ProtocolSide {
    kRequest     = 1,
    kResponse    = 2,
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolSide, {
    {ProtocolSide::kRequest,  1},
    {ProtocolSide::kResponse, 2},
})

///  @brief 业务上Body类型
enum class ProtocolBodyType {
    kUnknown   = 0,     //未知格式(没有设置)
    kJson      = 1,    // json格式
    kXml       = 2,    // xml格式
    kText      = 3,    // 纯文本
    kBinary    = 4,    // TCP 二进制数据流
    kMax,
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolBodyType, {
    {ProtocolBodyType::kUnknown, "unknown"},
    {ProtocolBodyType::kJson,    "json"},
    {ProtocolBodyType::kXml,     "xml"},
    {ProtocolBodyType::kText,    "text"},
    {ProtocolBodyType::kBinary,  "binart"},
})

enum class ProtocolStatus {
    kInvalid     = 0,
    kValid       = 1,
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolStatus,{
    {ProtocolStatus::kInvalid, 0},
    {ProtocolStatus::kValid,   1},
})

///  @brief 协议项是否上线到runtime
enum class ProtocolRuntimeEnabled {
    kOff = 0,
    kOn  = 1,
};
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolRuntimeEnabled,{
    {ProtocolRuntimeEnabled::kOff, 0},
    {ProtocolRuntimeEnabled::kOn,  1},
})

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