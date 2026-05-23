/**
 * @file custom_tcp_pattern_spec.h
 * @brief 自定义TCP协议格式定义
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-16 15:39:13
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT__CUSTOM_TCP_PATTERN_SPEC_H__
#define __KIT__CUSTOM_TCP_PATTERN_SPEC_H__

#include "domain/custom_tcp_field_model.h"
#include "nlohmann/json.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace kit_domain {


enum class LengthPolicy
{
    kUndefine,
    kBodyLength,    // 格式中有指示Body长度字段
    kTotalLength,   // 格式中有指示总长度字段
    kNoLength,      // 格式中没有长度字段
};
// nlohann::json 序列化反序列化
NLOHMANN_JSON_SERIALIZE_ENUM(LengthPolicy, {
    {LengthPolicy::kUndefine, "undef"},
    {LengthPolicy::kBodyLength, "body_length"},
    {LengthPolicy::kTotalLength, "total_length"},
    {LengthPolicy::kNoLength, "no_length"},
})

LengthPolicy LengthPolicyFromString(const std::string &str);
std::string LengthPolicyToString(LengthPolicy policy);



struct CustomTcpPatternSpec
{
    /// @brief 格式定义版本
    int32_t version{2}; 
    /// @brief 完整头部长度
    size_t header_bytes{0};
    /// @brief 默认每个字段 真值数组字节序顺序
    FieldByteOrder default_order{FieldByteOrder::kUndefine};
    LengthPolicy length_policy{LengthPolicy::kUndefine};
    /// @brief 格式字段集合
    FieldSpecSet fields;

    /// @brief from_json时用于校验使用
    std::unordered_map<FieldRole, int32_t> role_counts;

    /**
     * @brief 通过特殊字段进行查询
     * @param role 
     * @return std::vector<FieldSpec> 
     */
    const FieldSpec* byUniqueRole(FieldRole role) const;
    const FieldSpec* byPos(size_t pos) const;

    /**
     * @brief (s重难点)校验当前pattern合法性
     * @return true 
     * @return false 
     */
    bool validate() const;

    // nlohmann::json 序列化与反序列化
    friend void to_json(nlohmann::json& j, const CustomTcpPatternSpec& spec);
    friend void from_json(const nlohmann::json& j, CustomTcpPatternSpec& spec);

    static std::optional<CustomTcpPatternSpec> FromJson(const nlohmann::json &json);
};



}
#endif // __KIT__CUSTOM_TCP_PATTERN_SPEC_H__
