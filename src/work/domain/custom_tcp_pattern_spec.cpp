/**
 * @file custom_tcp_pattern_spec.cpp
 * @brief 自定义TCP协议格式定义
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-16 15:45:48
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_pattern_spec.h"
#include "domain/custom_tcp_field_model.h"
#include "domain/domain_log.h"


#include <iterator>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>

using nljson = nlohmann::json;

namespace kit_domain {

// pattern 校验辅助函数
namespace {

/**
 * @brief 检查Pattern基础信息
 * @param spec 
 * @return true 
 * @return false 
 */
inline bool ValidateBasicSpec(const CustomTcpPatternSpec &spec)
{
    if(2 != spec.version)
    {
        CUSTOM_F_ERROR("pattern 'version' value invalid: %d \n", spec.version);
        return false;
    }
    if(0 == spec.header_bytes)
    {
        CUSTOM_F_ERROR("pattern 'header_bytes' value invalid: %d \n", spec.header_bytes);
        return false;
    }
    if(FieldByteOrder::kUndefine == spec.default_order)
    {
        CUSTOM_F_ERROR("pattern 'default_order' value invalid: %d\n", static_cast<int>(spec.default_order));
        return false;
    }
    if(LengthPolicy::kUndefine == spec.length_policy)
    {
        CUSTOM_F_ERROR("pattern 'length_policy' value invalid: %d\n", static_cast<int>(spec.length_policy));
        return false;
    }
    if(spec.fields.empty())
    {
        CUSTOM_F_ERROR("pattern 'fields'  is empty\n");
        return false;
    }

    return true;
}

/**
 * @brief 检查字段配置的长度和类型是否对齐符合
 * @param spec 
 * @return true 
 * @return false 
 */
inline bool ValidateFieldTypeLength(const FieldSpec &spec)
{
    switch(spec.type)
    {
        case FieldType::kInt8:
        case FieldType::kUint8:
        {
            return spec.byte_len == 1;
        }
        case FieldType::kInt16:
        case FieldType::kUint16:
        {
            return spec.byte_len == 2;
        }
        case FieldType::kInt32:
        case FieldType::kUint32:
        case FieldType::kFloat:
        {
            return spec.byte_len == 4;
        }
        case FieldType::kInt64:
        case FieldType::kUint64:
        case FieldType::kDouble:
        {
            return spec.byte_len == 8;
        }
        case FieldType::kString:
        {
            return true;
        }
        default:
            CUSTOM_F_ERROR("undefine field 'type' \n");
            return false;
    }

    return true;
}

/**
 * @brief 检查Pattern中每个字段基础信息
 * @param spec 
 * @return true 
 * @return false 
 */
inline bool ValidateBasicFields(const CustomTcpPatternSpec &spec)
{
    size_t pre_bt = 0, pre_ed = 0;
    size_t bt = 0, ed = 0;
    for(auto it = spec.fields.begin(); it != spec.fields.end();++it)
    {
        const auto& field = *it;
        if(!field.validate())
        {
            CUSTOM_F_ERROR("field invliad! pos[%ld] name[%s] \n", field.byte_pos, field.name.c_str());
            return false;
        }

        if(field.byte_pos + field.byte_len > spec.header_bytes)
        {
            CUSTOM_F_ERROR("field range more header! pos[%ld] name[%s] len[%ld]\n", field.byte_pos, field.name.c_str(), field.byte_len);
            return false;
        }
        if(!ValidateFieldTypeLength(field)) 
        {
            CUSTOM_F_ERROR("ValidateFieldTypeLength failed!\n");
            return false;
        }

        bt = field.byte_pos;
        ed = bt + field.byte_len;
        // 检查字段长度范围是否存在重叠
        if(it != spec.fields.begin()
            && (pre_bt <= bt && pre_ed > bt))
        {
            --it;
            CUSTOM_F_ERROR("field range duplication! name[%s]: [%lu, %lu) --> name:[%s] [%lu, %lu)\n", field.name.c_str(), field.byte_pos, field.byte_pos + field.byte_len, 
            it->name.c_str(), it->byte_pos, it->byte_pos + it->byte_len);
            return false;
        }

        pre_bt = bt;
        pre_ed = ed;
        
    }

    return true;
}


/**
 * @brief 检查字段的 match_byte情况
 * @param spec 
 * @return true 
 * @return false 
 */
inline bool ValidateFieldMatchRule(const FieldSpec &spec)
{
    const auto& behavior = BehaviorOf(spec.role);

    if(behavior.require_match && !spec.match.has_value())
    {
        CUSTOM_F_ERROR("match require but not has value! pos[%ld] name[%s] role_name[%s] \n", spec.byte_pos, spec.name.c_str(), behavior.role_tag);
        return false;
    }

    if(!behavior.allow_match && spec.match.has_value())
    {
        CUSTOM_F_ERROR("match not allow but has value! pos[%ld] name[%s] role_name[%s] \n", spec.byte_pos, spec.name.c_str(), behavior.role_tag);
        return false;
    }

    if(spec.match.has_value() && spec.match->size() != spec.byte_len)
    {
        CUSTOM_F_ERROR("match real size != 'byte_len' ! pos[%ld] name[%s] role_name[%s] \n", spec.byte_pos, spec.name.c_str(), behavior.role_tag);
        return false;
    }

    return true;
}

/**
 * @brief 检查Pattern中每个字段对应的角色信息
 * @param spec 
 * @return true 
 * @return false 
 */
inline bool ValidateRoleRules(const CustomTcpPatternSpec &spec)
{
    const auto role_count_of = [&spec](FieldRole role) -> int32_t {
        const auto it = spec.role_counts.find(role);
        return it == spec.role_counts.end() ? 0 : it->second;
    };

    for(auto &field : spec.fields)
    {
        if(!ValidateFieldMatchRule(field))
        {
            return false;
        }
        const auto& be = BehaviorOf(field.role);

        // 检查字段唯一性
        if(be.required_unique && role_count_of(field.role) > 1)
        {
            CUSTOM_F_ERROR("unique require but not! pos[%ld] name[%s] role_name[%s] \n", field.byte_pos, field.name.c_str(), be.role_tag);
            return false;
        }

        // 检查是否可写入
        if(FieldWriteKind::kUnsupported ==  be.write_kind)
        {
            CUSTOM_F_ERROR("unsupport write! pos[%ld] name[%s] role_name[%s] \n", field.byte_pos, field.name.c_str(), be.role_tag);
            return false;
        }
    }
    // start_magic 必须存在且唯一存在
    if(role_count_of(FieldRole::kStartMagic) != 1)
    {
        CUSTOM_F_ERROR("role 'start_magic' not exist! \n");
        return false;
    }

    // function_code 必须存在且唯一存在
    if(role_count_of(FieldRole::kFunctionCode) != 1)
    {
        CUSTOM_F_ERROR("role 'function_code' not exist! \n");
        return false;
    }
    return true;
}


/**
 * @brief 检查Pattern中长度策略信息
 * @param spec 
 * @return true 
 * @return false 
 */
inline bool ValidateLengthPolicy(const CustomTcpPatternSpec &spec)
{
    const auto role_count_of = [&spec](FieldRole role) -> int32_t {
        const auto it = spec.role_counts.find(role);
        return it == spec.role_counts.end() ? 0 : it->second;
    };

    const int32_t body_len_role_count = role_count_of(FieldRole::kBodyLength);
    const int32_t total_len_role_count = role_count_of(FieldRole::kTotalLength);

    switch(spec.length_policy)
    {
        case LengthPolicy::kBodyLength:
        {
            return  1 == body_len_role_count && 0 == total_len_role_count;
        }
        case LengthPolicy::kTotalLength:
        {
            return  0 == body_len_role_count && 1 == total_len_role_count;
        }
        case LengthPolicy::kNoLength:
        {
            return  0 == body_len_role_count && 0 == total_len_role_count;
        }
        default:
            CUSTOM_F_ERROR("undefine length policy\n");
            return false;
    }
}

}



LengthPolicy LengthPolicyFromString(const std::string &str)
{
    if("body_length" == str)
    {
        return LengthPolicy::kBodyLength;
    }
    else if("total_length" == str)
    {
        return LengthPolicy::kTotalLength;
    }
    else if("no_length" == str)
    {
        return LengthPolicy::kNoLength;
    }

    throw std::invalid_argument("Invalid length policy string: " + str);
}

std::string LengthPolicyToString(LengthPolicy policy)
{
    switch(policy)
    {
        case LengthPolicy::kBodyLength: return "body_length";
        case LengthPolicy::kTotalLength: return "total_length";
        case LengthPolicy::kNoLength: return "no_length";
        default:
            throw std::invalid_argument("Invalid length policy: " + std::to_string(static_cast<int>(policy)));
    }
}

const FieldSpec* CustomTcpPatternSpec::byUniqueRole(FieldRole role) const
{
    const auto &ba = BehaviorOf(role);
    if(!ba.required_unique)
    {
        CUSTOM_F_ERROR("role not unique! role_tag[%s] \n", ba.role_tag);
        return nullptr;
    }

    for(auto &field : fields)
    {
        if(field.role == role)
        {
            return &field;
        }
    }
    return nullptr;
}

const FieldSpec *CustomTcpPatternSpec::byPos(size_t pos) const
{
    auto it = fields.find(pos);
    return  it == fields.end() ? nullptr : &(*it);
}


bool CustomTcpPatternSpec::validate() const
{
    if (!ValidateBasicSpec(*this)) 
    {
        CUSTOM_F_ERROR("ValidateBasicSpec failed!");
        return false;
    }

    if (!ValidateBasicFields(*this)) 
    {
        CUSTOM_F_ERROR("ValidateBasicFields failed!\n");
        return false;
    }

    if (!ValidateRoleRules(*this)) 
    {
        CUSTOM_F_ERROR("ValidateRoleRules failed!");
        return false;
    }

    if (!ValidateLengthPolicy(*this)) 
    {
        CUSTOM_F_ERROR("ValidateLengthPolicy failed!");
        return false;
    }

    return true;
}

void to_json(nlohmann::json& j, const CustomTcpPatternSpec& spec)
{
    j = nljson::object();
    j["version"] = spec.version;
    j["header_bytes"] = spec.header_bytes;
    j["default_order"] = FieldByteOrderToString(spec.default_order);
    j["length_policy"] = LengthPolicyToString(spec.length_policy);

    nljson fields_json = nljson::array();

    for(auto &feild : spec.fields)
    {
        const nlohmann::json item = feild;
        fields_json.push_back(item);
    }
    j["fields"] = std::move(fields_json);

}

void from_json(const nlohmann::json& j, CustomTcpPatternSpec& spec)
{
    spec.fields.clear();
    spec.role_counts.clear();

    j.at("version").get_to(spec.version);
    j.at("header_bytes").get_to(spec.header_bytes);
    spec.default_order = FieldByteOrderFromString(j.at("default_order").get<std::string>());
    spec.length_policy = LengthPolicyFromString(j.at("length_policy").get<std::string>());

    auto it = j.find("fields");
    if(it == j.end())
    {
        CUSTOM_F_ERROR("json field 'fields' not found \n");
        throw std::invalid_argument("json field 'fields' not found");
    }

    size_t real_header_bytes = 0;
    FieldSpec field; 
    for(auto &item : it->items())
    {
        if(item.value().empty())
        {
            continue;
        }
        item.value().get_to(field);
        field.byte_order = spec.default_order;
        if(!field.validate())
        {
            CUSTOM_F_ERROR("tcp pattern field invalid! %s \n", item.value().dump().c_str());

            throw std::invalid_argument("tcp pattern field invalid");
        }

        auto [it2, ok] = spec.fields.insert(field);
        if(!ok)
        {
            CUSTOM_F_ERROR("tcp pattern field duplicate! exist: pos[%d] name[%s] ---> cur: pos[%d] name[%s] \n", it2->byte_pos, it2->name.c_str(), field.byte_pos, field.name.c_str());
            throw std::runtime_error("duplicate pattern field");
        }
        // 顺带统计角色信息
        ++spec.role_counts[field.role];
        // 顺带统计头部长度
        real_header_bytes += field.byte_len;
    }
    if(spec.header_bytes != real_header_bytes)
    {
        CUSTOM_F_ERROR("json field 'header_bytes'[%ld] != fields define[%ld] \n", spec.header_bytes, real_header_bytes);

        throw std::invalid_argument("json field 'header_bytes' != fields define");
    }
}


std::optional<CustomTcpPatternSpec> CustomTcpPatternSpec::FromJson(const nlohmann::json &json)
{
    CustomTcpPatternSpec spec;

    if(json.empty())
    {
        CUSTOM_F_ERROR("json is null \n");
        return std::nullopt;
    }

    try 
    {
        json.get_to<CustomTcpPatternSpec>(spec);
        
        if(!spec.validate())
        {
            CUSTOM_F_ERROR("custom tcp pattern invalid! \n");
            return std::nullopt;
        }
    }
    catch (const std::exception &e) 
    {
        CUSTOM_F_ERROR("parse custom tcp pattern from json error: %s \n", e.what());

        return std::nullopt;
    }
    catch (...) 
    {
        CUSTOM_F_ERROR("parse custom tcp pattern from json unknow error\n");
        return std::nullopt;
    }



    return spec;
}


}
