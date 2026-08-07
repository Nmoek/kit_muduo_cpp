/**
 * @file custom_tcp_field_model.h
 * @brief 自定义TCP协议字段数据模型
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 00:31:11
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CUSTOM_TCP_FIELD_MODEL__
#define __KIT_CUSTOM_TCP_FIELD_MODEL__

#include "domain/custom_tcp_field_type_traits.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>


namespace kit_domain {

/**
 * @brief 字段角色
 */
enum class FieldRole
{
    kUndefine = -1,
    kCommon   = 0,          // 普通字段
    kStartMagic,      // 起始标识符
    kFunctionCode,    // 功能码
    kBodyLength,      // Body长度
    kTotalLength,     // 报文总长度
    kEndMagic,        // 结束标识符
    kChecksum         // 校验值
};
// nlohann::json 序列化反序列化
NLOHMANN_JSON_SERIALIZE_ENUM(FieldRole, {
    {FieldRole::kUndefine, "undef"},
    {FieldRole::kCommon, "common"},
    {FieldRole::kStartMagic, "start_magic"},
    {FieldRole::kFunctionCode, "function_code"},
    {FieldRole::kBodyLength, "body_length"},
    {FieldRole::kTotalLength, "total_length"},
    {FieldRole::kEndMagic, "end_magic"},
    {FieldRole::kChecksum, "check_sum"},
})
std::string FieldRoleToString(FieldRole role);

FieldRole FieldRoleFromString(const std::string& str);

/**
 * @brief 字段填充形式
 */
enum class FieldWriteKind
{
    kZeroFill,            // 零值填充
    kFixedMatch,          // 固定值填充
    kItemFunctionCode,    // 配置协议项-功能码值填充
    kItemFieldOverride,   // 配置协议项-其他类型值填充
    kAutoPatch,           // 自动根据报文内容填充
    kUnsupported,         // 不支持配置填充
};


/**
 * @brief 每个字段角色对应的动作模式
 */
struct FieldRoleBehavior
{

    /// @brief 行为规则对应哪个字段角色
    FieldRole role;
    /// @brief 是否是唯一字段(不代表必须出现，出现必须唯一)
    bool required_unique{false};
    /// @brief 是否必须配置 match
    bool require_match{false};
    /// @brief 是否允许配置 match
    bool allow_match{false};
    /// @brief 是否配置协议项能够覆盖赋值
    bool item_field_override_allowed{false};
    /// @brief 是否在 serialize 阶段自动计算并回写
    bool auto_patch{false};
    /// @brief 填充动作类型
    FieldWriteKind write_kind{FieldWriteKind::kZeroFill};
    /// @brief 字段角色标签字符串
    const char* role_tag;
};

/**
 * @brief 获取字段角色对应的动作模式
 * @param role 
 * @return const FieldRoleBehavior& 
 */
const FieldRoleBehavior& BehaviorOf(FieldRole role);


/**注意：这部分API需要暴露在外 方便后续替换实现方式**/
bool IsRequiredUnique(FieldRole role);

bool IsRequiresMatch(FieldRole role);
bool IsAllowsMatch(FieldRole role);

bool IsItemOverrideAllowed(FieldRole role);
bool IsAutoPatch(FieldRole role);
FieldWriteKind WriteKindOf(FieldRole role);
bool IsReservedUnsupported(FieldRole role);
std::string RoleTag(FieldRole role);

/**注意：这部分API需要暴露在外 方便后续替换实现方式**/

/**
 * @brief 字段值的大小端字节序
 */
enum class FieldByteOrder 
{
    kUndefine,
    kBigEndian,
    kLittleEndian,
    kRaw,
};
// nlohann::json 序列化反序列化
NLOHMANN_JSON_SERIALIZE_ENUM(FieldByteOrder, {
    {FieldByteOrder::kUndefine, "undef"},
    {FieldByteOrder::kBigEndian, "big"},
    {FieldByteOrder::kLittleEndian, "little"},
    {FieldByteOrder::kRaw, "raw"}
})

std::string FieldByteOrderToString(FieldByteOrder order);

FieldByteOrder FieldByteOrderFromString(const std::string& str);


/**
* @brief  字段定义
* 
*/
struct FieldSpec 
{  
    std::string name;
    size_t byte_pos{0};
    size_t byte_len{0};
    FieldType type{FieldType::kUndefine};
    FieldRole role{FieldRole::kUndefine};
    FieldByteOrder byte_order{FieldByteOrder::kUndefine};
    std::optional<std::vector<uint8_t>> match{std::nullopt};

    bool validate() const;

    friend void to_json(nlohmann::json& j, const FieldSpec& spec);
    friend void from_json(const nlohmann::json& j, FieldSpec& spec);
};

/**
 * @brief 字段值(bytes真值)
 */
struct FieldValue 
{
    FieldSpec spec;
    std::vector<uint8_t> bytes;

    /**
     * @brief 从数据流中提取数据
     * @param data 
     * @return true 
     * @return false 
     */
    bool extract(const std::vector<uint8_t> &data);
    /**
     * @brief 输出bytes十六进制字符串  H1234
     * @return std::string 
     */
    std::string hex() const;
    /**
     * @brief 输出bytes编译期值
     * @return FieldScalar 
     */
    FieldScalar scalar() const;

    /**
     * @brief 若当前字段描述是长度值 可以直接解析
     * @return uint64_t 
     */
    uint64_t asUnsignedLength() const;
};

struct CustomTcpFieldCompare
{
    using is_transparent = std::true_type;

    bool operator()(const FieldSpec& a, const FieldSpec& b) const
    {
        // 字段起始字节位置 小->大
        return a.byte_pos < b.byte_pos;
    }

    // 支持异构查找--按照byte_pos
    bool operator()(size_t a, const FieldSpec& b) const
    {
        return a < b.byte_pos;
    }
    // 支持异构查找--按照byte_pos
    bool operator()(const FieldSpec& a, size_t b) const
    {
        return a.byte_pos < b;
    }
};
/**
 * @brief 按字段pos位置排序的字段描述集合
 */
using FieldSpecSet =  std::set<FieldSpec, CustomTcpFieldCompare>;
/**
 * @brief 按字段pos位置排序的完整字段集合
 */
using FieldValueMap = std::map<size_t, FieldValue>;

std::pair<FieldValueMap, size_t> FieldValueMapParseFromJson(const nlohmann::json &root);


}
#endif //__KIT_CUSTOM_TCP_FIELD_MODEL__
