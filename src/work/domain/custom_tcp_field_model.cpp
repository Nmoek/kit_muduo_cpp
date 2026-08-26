/**
 * @file custom_tcp_field_model.cpp
 * @brief 自定义TCP协议字段数据模型
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 16:11:26
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_field_model.h"
#include "domain/custom_tcp_field_codec.h"
#include "net/net_data_converter.h"
#include "domain/domain_log.h"



#include <array>
#include <functional>
#include <stdexcept>
#include <type_traits>

using namespace kit_muduo;

namespace kit_domain {

inline static bool CommonFieldValidate(const FieldValue &cfg, const FieldValue &real)
{
    return cfg.bytes == real.bytes;
}

static const std::unordered_map< FieldRole, FieldRoleBehavior> sg_field_role_behavior_table{
    {FieldRole::kCommon, {FieldRole::kCommon, false, false, false, true, false, FieldWriteKind::kItemFieldOverride,"common"}},
    {FieldRole::kStartMagic, {FieldRole::kStartMagic, true, true, true, false, false, FieldWriteKind::kFixedMatch, "start_magic"}},
    {FieldRole::kFunctionCode, {FieldRole::kFunctionCode, true, false, false, false, false, FieldWriteKind::kItemFunctionCode, "function_code"}},
    {FieldRole::kBodyLength, {FieldRole::kBodyLength, true, false, false, false, true, FieldWriteKind::kAutoPatch, "body_length"}},
    {FieldRole::kTotalLength, {FieldRole::kTotalLength, true, false, false, false, true, FieldWriteKind::kAutoPatch, "total_length"}},
    {FieldRole::kEndMagic, {FieldRole::kEndMagic, true, false, false, false, false, FieldWriteKind::kUnsupported, "end_magic"}},
    {FieldRole::kChecksum, {FieldRole::kChecksum, true, false, false, false, false, FieldWriteKind::kUnsupported, "checksum"}},
};

static const std::unordered_map<FieldRole, std::string> sg_role2str{
    {FieldRole::kCommon, "common"},
    {FieldRole::kStartMagic, "start_magic"},
    {FieldRole::kFunctionCode, "function_code"},
    {FieldRole::kBodyLength, "body_length"},
    {FieldRole::kTotalLength, "total_length"},
    {FieldRole::kEndMagic, "end_magic"},
    {FieldRole::kChecksum, "checksum"},
};

static const std::unordered_map<std::string, FieldRole> sg_str2role{
    {"common", FieldRole::kCommon},
    {"start_magic", FieldRole::kStartMagic},
    {"function_code", FieldRole::kFunctionCode},
    {"body_length", FieldRole::kBodyLength},
    {"total_length", FieldRole::kTotalLength},
    {"end_magic", FieldRole::kEndMagic},
    {"checksum", FieldRole::kChecksum},
    {"check_sum", FieldRole::kChecksum},
};

std::string FieldRoleToString(FieldRole role)
{
    return sg_role2str.at(role);
}

FieldRole FieldRoleFromString(const std::string& str)
{
    return sg_str2role.at(str);
}

const FieldRoleBehavior& BehaviorOf(FieldRole role)
{
    auto it =  sg_field_role_behavior_table.find(role);
    if(it == sg_field_role_behavior_table.end())
    {
        throw std::invalid_argument("undefien field role");
    }
    return it->second;
}

std::string FieldByteOrderToString(FieldByteOrder order)
{
    switch (order) 
    {
        case FieldByteOrder::kBigEndian: return "big";
        case FieldByteOrder::kLittleEndian: return "little";
        case FieldByteOrder::kRaw: return "raw";
        default:
            throw std::invalid_argument("unknown byte order");
    }
}

FieldByteOrder FieldByteOrderFromString(const std::string& str)
{
    if("big" == str) 
    {
        return FieldByteOrder::kBigEndian;
    }
    else if("little" == str)
    {
        return FieldByteOrder::kLittleEndian;
    }
    else if("raw" == str)
    {
        return FieldByteOrder::kRaw;
    }

    throw std::invalid_argument("unknown byte order");
}

bool FieldSpec::validate() const
{
    if(0 == byte_len)
    {
        CUSTOM_F_ERROR("field 'byte_len' value invalid: %ld \n", byte_len);
        return false;
    }
    if(FieldType::kUndefine == type)
    {
        CUSTOM_F_ERROR("field 'type' value invalid: %d \n", static_cast<int>(type));
        return false;
    }
    if(FieldRole::kUndefine == role)
    {
        CUSTOM_F_ERROR("field 'role' value invalid: %d \n", static_cast<int>(role));
        return false;
    }
    if(FieldByteOrder::kUndefine == byte_order)
    {
        CUSTOM_F_ERROR("field 'byte_order' value invalid: %d \n", static_cast<int>(byte_order));
        return false;
    }
    if(FieldType::kString == type && (byte_len <= 0 || byte_len > 32))
    {
        CUSTOM_F_ERROR("field 'byte_len' value invalid for string: %lu \n", byte_len);
        return false;
    }
    return true;
}


void to_json(nlohmann::json& j, const FieldSpec& spec)
{
    j["name"] = spec.name;
    j["byte_pos"] = spec.byte_pos;
    j["byte_len"] = spec.byte_len;
    j["type"] = FieldTypeToString(spec.type);
    j["role"] = FieldRoleToString(spec.role);
    
    // json 中不显式暴露 byte_order，PatternSpec 级 default_order 会统一注入。

    if(spec.match.has_value())
    {
        j["match"] = BytesToHexString(*spec.match, "");
    }
}

void from_json(const nlohmann::json& j, FieldSpec& spec)
{
    j.at("name").get_to(spec.name);
    j.at("byte_pos").get_to(spec.byte_pos);
    j.at("byte_len").get_to(spec.byte_len);
    spec.type = FieldTypeFromString(j.at("type").get<std::string>());

    spec.role = FieldRoleFromString(j.at("role").get<std::string>());

    // j.at("byte_order").get_to(spec.byte_order);

    auto it = j.find("match");
    if(it == j.end())
    {
        spec.match = std::nullopt;
    }
    else
    {
        spec.match = HexStringToBytes(it.value().get<std::string>());
    }
}


bool IsRequiredUnique(FieldRole role)
{
    return BehaviorOf(role).required_unique;
}

bool IsRequiresMatch(FieldRole role)
{
    return BehaviorOf(role).require_match;
}

bool IsAllowsMatch(FieldRole role)
{
    return BehaviorOf(role).allow_match;
}

bool IsItemOverrideAllowed(FieldRole role)
{
    return BehaviorOf(role).item_field_override_allowed;
}

bool IsAutoPatch(FieldRole role)
{
    return BehaviorOf(role).auto_patch;
}

FieldWriteKind WriteKindOf(FieldRole role)
{
    return BehaviorOf(role).write_kind;
}

bool IsReservedUnsupported(FieldRole role)
{
    return BehaviorOf(role).write_kind == FieldWriteKind::kUnsupported;
}

std::string RoleTag(FieldRole role)
{
    return BehaviorOf(role).role_tag;
}

bool FieldValue::extract(const std::vector<uint8_t> &data)
{
    int32_t begin_pos = spec.byte_pos;
    int32_t end_pos = begin_pos + spec.byte_len;

    if(begin_pos >= data.size()
        ||  end_pos > data.size() 
        || begin_pos > end_pos)
    {
        CUSTOM_F_ERROR("field out of range! pos[%ld] len[%ld] name[%s]\n", spec.byte_pos, spec.byte_len, spec.name.c_str());
        return false;
    }

    bytes.assign(data.begin() + begin_pos, data.begin() + end_pos);

    return true;
}

std::string FieldValue::hex() const
{
    return BytesToHexString(bytes, "");
}

FieldScalar FieldValue::scalar() const
{
    return DecodeField(spec, bytes);
}

uint64_t FieldValue::asUnsignedLength() const
{
    if(FieldRole::kBodyLength != spec.role
        && FieldRole::kTotalLength != spec.role)
    {
        throw std::invalid_argument("field role is not length role");
    }


    return std::visit([](const auto&& value) ->uint64_t {
        using T = std::decay_t<decltype(value)>;


        if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>)
        {
            return static_cast<uint64_t>(value);
        }
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
        {
            if(value < 0)
            {
                throw std::invalid_argument("length field value < 0");
            }
            return static_cast<uint64_t>(value);
        }
        else
        {
            throw std::invalid_argument("length field must be integer");
        }

    }, DecodeField(spec, bytes));
}

std::pair<FieldValueMap, size_t> FieldValueMapParseFromJson(const nlohmann::json &root)
{
    FieldValueMap map;
    size_t total_len = 0;
    auto it = root.find("fields");
    if(it == root.end() || !it.value().is_array())
    {
        CUSTOM_F_ERROR("json field 'fields' not found, or not array\n");
        throw std::invalid_argument("json field 'fields' not found, or not array");
    }


    FieldValue field; 
    for(auto &item : root.at("fields"))
    {
        it = item.find("spec");
        if(it == item.end() || !it->is_object())
        {
            CUSTOM_F_ERROR("json field 'fields.spec' not found, or not object\n");
            throw std::invalid_argument("binary fields json invalid: " + item.dump());
        }

        it.value().get_to(field.spec);
        // TODO: 旧二进制 Body 配置暂时固定按大端解析；后续明确其字节序来源，
        // 再决定接入协议项 is_endian、项目 Pattern default_order 或独立字段配置。
        field.spec.byte_order = FieldByteOrder::kBigEndian;
        if(!field.spec.validate())
        {
            CUSTOM_F_ERROR("tcp message fields invalid! %s \n", it.value().dump().c_str());

            throw std::invalid_argument("binary fields json invalid: " + item.dump());
        }

        it = item.find("value");
        if(it != item.end() && it->is_string() && !it.value().empty())
        {
            field.bytes = kit_muduo::HexStringToBytes(it.value().get<std::string>());
        }
        total_len += field.spec.byte_len;

        auto [it2, ok] = map.emplace(field.spec.byte_pos,std::move(field));
        if(!ok)
        {
            CUSTOM_F_ERROR("binary fields field duplicate! exist: pos[%d] name[%s] ---> cur: pos[%d] name[%s] \n", it2->second.spec.byte_pos, it2->second.spec.name.c_str(), field.spec.byte_pos, field.spec.name.c_str());

            throw std::runtime_error("binary fields field duplicate");
        }

    }
    return {std::move(map), total_len};
}



}
