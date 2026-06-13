/**
 * @file protocol_runtime_key.h
 * @brief 协议运行键生成
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-09
 */
#ifndef __KIT_DOMAIN_PROTOCOL_RUNTIME_KEY_H__
#define __KIT_DOMAIN_PROTOCOL_RUNTIME_KEY_H__

#include "domain/type.h"
#include "nlohmann/json.hpp"

#include <optional>
#include <string>

namespace kit_domain {

/**
 * @brief 生成协议项唯一运行键值
 * @param type 协议类型
 * @param req_cfg 请求匹配配置
 * @return std::optional<std::string> 配置可运行时返回运行键，否则返回 nullopt
 */
std::optional<std::string> GenerateProtocolRuntimeKey(ProtocolType type,
                                                      const nlohmann::json &req_cfg);

} // namespace kit_domain

#endif // __KIT_DOMAIN_PROTOCOL_RUNTIME_KEY_H__
