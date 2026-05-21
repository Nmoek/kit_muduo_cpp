/**
 * @file custom_tcp_pattern_v2.cpp
 * @brief 自定义TCP协议格式V2
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-18 17:47:35
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_pattern_v2.h"
#include "domain/domain_log.h"

#include <algorithm>
#include <stdexcept>

namespace kit_domain {

int32_t CustomTcpPatternV2::checkStartMagic(const std::vector<FieldValue>& fields_value)
{
    ParseHeaderResult result;
    auto it = std::find_if(fields_value.begin(), fields_value.end(), [](const FieldValue &v){
        return FieldRole::kStartMagic == v.spec.role; 
    });
    if(it == fields_value.end())
    {
        CUSTOM_F_ERROR("role 'start_magic' not found!\n");
        return ParseHeaderResult::kPatternError;
    }

    // 与预先设置的校验值比对
    if(!it->spec.match.has_value() || it->spec.match.value() != it->bytes)
    {
        CUSTOM_F_ERROR("field 'start_magic' data invalid: %s\n", it->hex().c_str());
        return ParseHeaderResult::kInvalidDataError;
    }
    return ParseHeaderResult::kOk;
}

int32_t CustomTcpPatternV2::checkFunctionCode(const std::vector<FieldValue>& fields_value, std::string& func_code_hex)
{
    auto it = std::find_if(fields_value.begin(), fields_value.end(), [](const FieldValue &v){
        return v.spec.role == FieldRole::kFunctionCode; 
    });
    if(it == fields_value.end())
    {
        CUSTOM_F_ERROR("role 'function_code' not found!\n");
        return  ParseHeaderResult::kPatternError;
    }
    func_code_hex = it->hex();

    CUSTOM_F_DEBUG("function_code_hex: %s \n", func_code_hex.c_str());
    return  ParseHeaderResult::kOk;
}


bool CustomTcpPatternV2::WriteAt(std::vector<uint8_t> &headers_data, const FieldSpec& field_spec, const std::vector<uint8_t>& cfg_bytes)
{
    if(field_spec.byte_len != cfg_bytes.size())
    {
        CUSTOM_F_ERROR("write byte_len[%ld] != cfg_bytes[%ld] \n", field_spec.byte_len, cfg_bytes.size());
        return false;
    }
    if(field_spec.byte_pos + field_spec.byte_len > headers_data.size())
    {
        CUSTOM_F_ERROR("write field out of range [%ld] != cfg_bytes[%ld] \n", field_spec.byte_len, cfg_bytes.size());
        return false;
    }
    std::copy(cfg_bytes.begin(), cfg_bytes.end(), headers_data.begin() + field_spec.byte_pos);
    return true;
}

std::vector<uint8_t> CustomTcpPatternV2::ParseFromHex(const FieldSpec& field_spec, const std::string &hex_str)
{
    const auto& bytes = kit_muduo::HexStringToBytes(hex_str);
    if(bytes.size() != field_spec.byte_len)
    {
        CUSTOM_F_ERROR("ParseFromHex length mismatch! name[%s] byte_pos[%ld] byte_len[%ld] <---> hex[%s]\n", field_spec.name.c_str(), field_spec.byte_pos, field_spec.byte_len, hex_str.c_str());
        throw std::invalid_argument("hex field byte length mismatch");
    }

    return bytes;
}

bool CustomTcpPatternV2::PatchUnsignedLength(std::vector<uint8_t> &headers_data, const FieldSpec& field, size_t value)
{
    try 
    {
        FieldScalar scalar = MakeLengthScalar(field, value);
        const auto& bytes = EncodeField(field, scalar);

        if(field.byte_pos + field.byte_len > headers_data.size()
            || field.byte_len != bytes.size())
        {
            CUSTOM_F_ERROR("length value invalid!\n");
            return false;
        }

        std::copy(bytes.begin(), bytes.end(), headers_data.begin() + field.byte_pos);
        return true;
    }
    catch(const std::exception &e)
    {
        CUSTOM_F_ERROR("length value invalid: %s\n", e.what());
        return false;
    }
}

FieldScalar CustomTcpPatternV2::MakeLengthScalar(const FieldSpec& field, size_t value)
{
    switch(field.type)
    {
#define XX(ENUM_TYPE, TYPE) \
        case ENUM_TYPE: \
        {\
            if(value > std::numeric_limits<TYPE>::max()) \
            { \
                throw std::overflow_error("length overflow: '" #TYPE "'"); \
            }\
            return static_cast<TYPE>(value);\
        }

        XX(FieldType::kUint8, uint8_t);
        XX(FieldType::kInt8, int8_t);
        XX(FieldType::kUint16, uint16_t);
        XX(FieldType::kInt16, int16_t);
        XX(FieldType::kUint32, uint32_t);
        XX(FieldType::kInt32, int32_t);
        XX(FieldType::kUint64, uint64_t);
        XX(FieldType::kInt64, int64_t);
#undef XX
        default:
            throw std::invalid_argument("unsupport length type: " + FieldTypeToString(field.type));
    }
}

BodyLengthPattern::BodyLengthPattern(const CustomTcpPatternSpec &spec)
    :CustomTcpPatternBaseV2<BodyLengthPattern>(spec)
{

}

CustomTcpPatternType BodyLengthPattern::getPatternType() const 
{
    return CustomTcpPatternType::BODY_LENGTH_DEP;
}
const CustomTcpPatternSpec& BodyLengthPattern::spec() const
{
    return pattern_spec_;
}

bool BodyLengthPattern::remainBodyBytes(const std::vector<FieldValue>& fields_value, uint64_t& remain_bytes) const
{
    auto it = std::find_if(fields_value.begin(), fields_value.end(), [](const FieldValue& v){
        return FieldRole::kBodyLength == v.spec.role; 
    });
    if(it == fields_value.end())
    {
        CUSTOM_F_ERROR("role 'body_length' not found!\n");
        return false;
    }
    remain_bytes = it->asUnsignedLength();
    return true;
}

bool BodyLengthPattern::patchLength(std::vector<uint8_t>& headers_data, size_t body_length) const
{
    const FieldSpec* body_spec = pattern_spec_.byUniqueRole(FieldRole::kBodyLength);
    if(nullptr == body_spec)
    {
        CUSTOM_F_ERROR("role 'body_length' missing! \n");
        return false;
    }
    CUSTOM_F_DEBUG("patchLength::body_length: %ld \n", body_length);

    return PatchUnsignedLength(headers_data, *body_spec, body_length);

}

TotalLengthPattern::TotalLengthPattern(const CustomTcpPatternSpec &spec)
    :CustomTcpPatternBaseV2<TotalLengthPattern>(spec)
{

}


CustomTcpPatternType TotalLengthPattern::getPatternType() const
{
    return CustomTcpPatternType::TOTAL_LENGTH_DEP;
}

const CustomTcpPatternSpec& TotalLengthPattern::spec() const 
{
    return pattern_spec_;
}

bool TotalLengthPattern::remainBodyBytes(const std::vector<FieldValue>& fields_value, uint64_t& remain_bytes) const
{
    auto it = std::find_if(fields_value.begin(), fields_value.end(), [](const FieldValue& v){
        return FieldRole::kTotalLength == v.spec.role; 
    });
    if(it == fields_value.end())
    {
        CUSTOM_F_ERROR("role 'total_length' not found!\n");
        return false;
    }
    uint64_t total_length = it->asUnsignedLength();
    if(total_length < spec().header_bytes)
    {
        CUSTOM_F_ERROR("total length invalid! total_length[%ld] < header_bytes[%ld]\n", total_length, spec().header_bytes);
        return false;
    }
    remain_bytes = total_length - spec().header_bytes;
    return true;
}

bool TotalLengthPattern::patchLength(std::vector<uint8_t>& headers_data, size_t body_length) const
{
    const FieldSpec* total_spec = pattern_spec_.byUniqueRole(FieldRole::kTotalLength);
    if(nullptr == total_spec)
    {
        CUSTOM_F_ERROR("role 'total_length' missing! \n");
        return false;
    }
    size_t total_length = headers_data.size() + body_length;
    if(total_length < headers_data.size())
    {
        CUSTOM_F_WARN("total_length invalid! total_length[%ld] < headers_bytes[%ld] \n", total_length, headers_data.size());
        total_length = headers_data.size();
    }

    CUSTOM_F_DEBUG("patchLength::total_length: %ld \n", total_length);

    return PatchUnsignedLength(headers_data, *total_spec, total_length);
}


NoLengthPattern::NoLengthPattern(const CustomTcpPatternSpec &spec)
    :CustomTcpPatternBaseV2<NoLengthPattern>(spec)
{

}

CustomTcpPatternType NoLengthPattern::getPatternType() const
{
    return CustomTcpPatternType::NO_LENGTH_DEP;
}

const CustomTcpPatternSpec& NoLengthPattern::spec() const 
{
    return pattern_spec_;
}

bool NoLengthPattern::remainBodyBytes(const std::vector<FieldValue>& fields_value, uint64_t& remain_bytes) const
{
    remain_bytes = 0;
    return true;
}

bool NoLengthPattern::patchLength(std::vector<uint8_t>& headers_data, size_t length) const
{
    CUSTOM_F_INFO("policy 'no_length' dont need length \n");
    return true;
}

}