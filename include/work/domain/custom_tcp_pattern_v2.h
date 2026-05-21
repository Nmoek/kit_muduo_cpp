/**
 * @file custom_tcp_pattern_v2.h
 * @brief 自定义TCP协议格式V2
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-18 17:03:58
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CUSTOM_TCP_PATTERN_V2_H__
#define __KIT_CUSTOM_TCP_PATTERN_V2_H__


#include "domain/custom_tcp_field_codec.h"
#include "domain/custom_tcp_field_model.h"
#include "domain/custom_tcp_field_type_traits.h"
#include "domain/custom_tcp_pattern_spec.h"
#include "domain/custom_tcp_protocol_item.h"
#include "domain/type.h"

#include <exception>
#include <vector>
#include <string>


namespace kit_domain {


class CustomTcpPatternV2
{
public:
    struct ParseHeaderResult
    {
        enum 
        {
            kOk,
            kNonMinLength,      // 当前数据未达到最小解析长度(头长度不足)
            kInvalidDataError,  // 非法数据
            kPatternError,      // 格式错误
            kCalcLengthError,   // 报文长度计算错误
            kChecksumError,     // 校验和错误
        };
        /// @brief 解析状态
        int32_t status{kOk};
        /// @brief 解析得到的真实字段+真实字段值
        std::vector<FieldValue> fields_value;
        /// @brief 解析得到的真实功能码
        std::string function_code_hex;
        /// @brief 剩余未解析Body长度
        uint64_t remain_body_bytes{0};
        
        bool ok() { return status == kOk; }
    };

    virtual ~CustomTcpPatternV2() = default;

    virtual CustomTcpPatternType getPatternType() const = 0;
    virtual const CustomTcpPatternSpec& spec() const = 0;
    virtual ParseHeaderResult parseHeader(const std::vector<uint8_t> &data) = 0;
    virtual std::optional<std::vector<uint8_t>> serialize(const CustomTcpItemCfg &item_cfg, const std::vector<uint8_t> &body_data) const = 0;

protected:
    int32_t checkStartMagic(const std::vector<FieldValue>& fields_value);

    int32_t checkFunctionCode(const std::vector<FieldValue>& fields_value, std::string& func_code_hex);

    static bool WriteAt(std::vector<uint8_t> &headers_data, const FieldSpec& field_spec, const std::vector<uint8_t>& cfg_bytes);

    static std::vector<uint8_t> ParseFromHex(const FieldSpec& field_spec, const std::string &hex_str);

    static bool PatchUnsignedLength(std::vector<uint8_t> &headers_data, const FieldSpec& field, size_t value);

    static FieldScalar MakeLengthScalar(const FieldSpec& field, size_t value);
};


template<class Derived>
class CustomTcpPatternBaseV2 : public CustomTcpPatternV2 
{
public:
    CustomTcpPatternBaseV2(const CustomTcpPatternSpec &spec)
        :pattern_spec_(spec)
    {

    }

    ~CustomTcpPatternBaseV2() override = default;

    ParseHeaderResult parseHeader(const std::vector<uint8_t> &data) override
    {
        ParseHeaderResult result;
        const CustomTcpPatternSpec& pattern_spec = derived().spec();

        // 1. 校验 data.size >= header_bytes
        if(pattern_spec.header_bytes > data.size()) 
        {
            CUSTOM_F_ERROR("data len not enough, data bytes[%lu] < heaer bytes[%lu]", data.size(), pattern_spec.header_bytes);

            result.status = ParseHeaderResult::kNonMinLength;
            return result;
        }

        // 初始化解析数组
        result.fields_value.reserve(pattern_spec.fields.size());

        // 2. 将所有字段解析
        for(auto &cfg_field_spec : pattern_spec.fields)
        {
            FieldValue field;
            field.spec = cfg_field_spec;
            if(!field.extract(data))
            {
                CUSTOM_F_ERROR("field extract error! name[%s] byte_pos[%lu] role_tag[%s] \n", cfg_field_spec.name.c_str(), cfg_field_spec.byte_pos, RoleTag(cfg_field_spec.role).c_str());
                result.status = ParseHeaderResult::kPatternError;
                return result;
            }
            CUSTOM_F_DEBUG("Field extract: name[%s], byte_pos[%d], byte_len[%d], role_tag[%s], bytes[%s]\n", 
                cfg_field_spec.name.c_str(), cfg_field_spec.byte_pos, cfg_field_spec.byte_len, RoleTag(cfg_field_spec.role).c_str(),
                field.hex().c_str());

            result.fields_value.emplace_back(field);
        }


        // 3. 找到关键字段单独校验: StartMagic / FunctionCode / optional Length
        // 3.1 提取起始标识符 StartMagic
        result.status = checkStartMagic(result.fields_value);
        if(!result.ok())
        {
            return result;
        }

        // 3.2 提取功能码 FunctionCode
        result.status = checkFunctionCode(result.fields_value, result.function_code_hex);
        if(!result.ok())
        {
            return result;
        }

        // 3. 派生类计算 remain_body_bytes
        if(!derived().remainBodyBytes(result.fields_value, result.remain_body_bytes))
        {
            CUSTOM_F_ERROR("policy '%s' remainBodyBytes error\n",  LengthPolicyToString(pattern_spec.length_policy).c_str());
            result.status = ParseHeaderResult::kCalcLengthError;
            return result;
        }

        // 4. 返回已解析字段集合
        return result;
    }

    std::optional<std::vector<uint8_t>> serialize(const CustomTcpItemCfg &item_cfg, const std::vector<uint8_t> &body_data) const override
    {
        const auto &pattern_spec = derived().spec();
        std::vector<uint8_t> headers_data(pattern_spec.header_bytes, 0x00);

        try
        {
            for(auto &field : pattern_spec.fields)
            {
                switch(WriteKindOf(field.role))
                {
                    case FieldWriteKind::kZeroFill:
                    {
                        // 什么都不做保持填充0
                        break;
                    }
                    case FieldWriteKind::kFixedMatch:
                    {
                        if(!field.match.has_value() || !WriteAt(headers_data, field, field.match.value()))
                        {
                            CUSTOM_F_ERROR("write match error! name[%s] pos[%ld] \n", field.name.c_str(), field.byte_pos);
                            return std::nullopt;
                        }
                        
                        break;
                    }
                    case FieldWriteKind::kItemFunctionCode:
                    {
                        const auto& bytes = ParseFromHex(field, item_cfg.function_code_hex);

                        if(!WriteAt(headers_data, field, bytes))
                        {
                            CUSTOM_F_ERROR("write item function code error! name[%s] pos[%ld] \n", field.name.c_str(), field.byte_pos);
                            return std::nullopt;
                        }
                        
                        break;
                    }
                    case FieldWriteKind::kItemFieldOverride:
                    {
                        auto it = item_cfg.field_values_by_byte_pos.find(field.byte_pos);

                        // 注意: 这里语义允许字段值只配置部分
                        if(it == item_cfg.field_values_by_byte_pos.end())
                        {
                            CUSTOM_F_DEBUG("item override not set! name[%s] byte_pos[%ld] \n", field.name.c_str(), field.byte_pos);

                            break;
                        }

                        if(!WriteAt(headers_data, field, it->second))
                        {
                            CUSTOM_F_ERROR("write item override error! name[%s] byte_pos[%ld] \n", field.name.c_str(), field.byte_pos);
                            return std::nullopt;
                        }
                        
                        break;
                    }
                    case FieldWriteKind::kAutoPatch:
                    {
                        // 什么都不做后续 根据长度策略自动填充
                        break;
                    }
                    case FieldWriteKind::kUnsupported:
                    {
                        CUSTOM_F_ERROR("write field unsupport! name[%s] byte_pos[%ld] role_tag[%s]\n", field.name.c_str(), field.byte_pos, RoleTag(field.role).c_str());
                        return std::nullopt;
                    }
                    default:
                        CUSTOM_F_ERROR("undefine write kind\n");
                        return std::nullopt;
                }
            }

            if(!derived().patchLength(headers_data, body_data.size()))
            {
                CUSTOM_F_ERROR("policy '%s' patchLength error\n",  LengthPolicyToString(pattern_spec.length_policy).c_str());
                return std::nullopt;
            }

            std::vector<uint8_t> data;
            // 预留空间避免扩容
            data.reserve(headers_data.size() + body_data.size());
            // 填充头部字段数据
            data.insert(data.end(), headers_data.begin(), headers_data.end());
            // 填充Body数据
            data.insert(data.end(), body_data.begin(), body_data.end());

            return data;
        }
        catch(const std::exception &e)
        {
            CUSTOM_F_ERROR("serialize exception: %s \n", e.what());
            return std::nullopt;
        }
    }

private:
    const Derived& derived() const
    {
        return static_cast<const Derived&>(*this);
    }

protected:
    CustomTcpPatternSpec pattern_spec_;
};

class BodyLengthPattern final: public CustomTcpPatternBaseV2<BodyLengthPattern>
{
public:
    BodyLengthPattern(const CustomTcpPatternSpec &spec);

    ~BodyLengthPattern() override = default;

    CustomTcpPatternType getPatternType() const override;
    const CustomTcpPatternSpec& spec() const override;

    bool remainBodyBytes(const std::vector<FieldValue>& fields_value, uint64_t& remain_bytes) const;

    bool patchLength(std::vector<uint8_t>& headers_data, size_t body_length) const;
};


class TotalLengthPattern final: public CustomTcpPatternBaseV2<TotalLengthPattern>
{
public:
    TotalLengthPattern(const CustomTcpPatternSpec &spec);

    ~TotalLengthPattern() override = default;

    CustomTcpPatternType getPatternType() const override;
    const CustomTcpPatternSpec& spec() const override;

    bool remainBodyBytes(const std::vector<FieldValue>& fields_value, uint64_t& remain_bytes) const;
    
    bool patchLength(std::vector<uint8_t>& headers_data, size_t body_length) const;
};


class NoLengthPattern final: public CustomTcpPatternBaseV2<NoLengthPattern>
{
public:
    NoLengthPattern(const CustomTcpPatternSpec &spec);

    ~NoLengthPattern() override = default;

    CustomTcpPatternType getPatternType() const override;
    const CustomTcpPatternSpec& spec() const override;

    bool remainBodyBytes(const std::vector<FieldValue>& fields_value, uint64_t& remain_bytes) const;

    bool patchLength(std::vector<uint8_t>& headers_data, size_t body_length) const;
};

}
#endif //__KIT_CUSTOM_TCP_PATTERN_V2_H__