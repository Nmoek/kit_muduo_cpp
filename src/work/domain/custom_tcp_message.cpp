/**
 * @file custom_tcp_message.cpp
 * @brief 自定义TCP消息(请求/响应一体)
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 18:51:40
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/custom_tcp_field_model.h"
#include "domain/domain_log.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_context.h"
#include "net/net_data_converter.h"
#include <optional>


namespace kit_domain {


CustomTcpMessage::CustomTcpMessage()
    :recordTime_(0)
{

}

void CustomTcpMessage::addField(const FieldValue &field_value)
{
    header_fields_by_byte_pos_[field_value.spec.byte_pos] = field_value;
}

const FieldValue* CustomTcpMessage::getField(size_t byte_pos) const
{
    auto it = header_fields_by_byte_pos_.find(byte_pos);
    return it == header_fields_by_byte_pos_.end() ? nullptr : &it->second;
}

void CustomTcpMessage::appendBodyData(const char* start, size_t len)
{
    if(start == nullptr || len == 0)
    {
        return;
    }
    body_data_.insert(body_data_.end(),
        reinterpret_cast<const uint8_t*>(start),
        reinterpret_cast<const uint8_t*>(start + len));
}

void CustomTcpMessage::appendBodyData(const std::vector<uint8_t>& data)
{
    body_data_.insert(body_data_.end(), data.begin(), data.end());
}


uint64_t CustomTcpMessage::getHeaderLen() const
{
    uint64_t res = 0;
    for(auto &it : header_fields_by_byte_pos_)
    {
        res += it.second.spec.byte_len;
    }
    return res;
}


std::string CustomTcpMessage::toHeaderString() const
{
    std::string data;
    for(auto &field : header_fields_by_byte_pos_)
    {
        data += field.second.hex() + " ";
    }

    return data;
}


std::optional<std::vector<uint8_t>> CustomTcpMessage::toBytes() const
{
    uint64_t header_len = getHeaderLen();
    if(header_len <= 0)
    {
        CUSTOM_F_ERROR("custom tcp pattern header_len invalid\n");
        return std::nullopt;
    }

    std::vector<uint8_t> headers_data(header_len, 0x00);

    try
    {

        for(auto &[pos, filed_value] : header_fields_by_byte_pos_)
        {
            const auto& spec = filed_value.spec;
            const auto &bytes = filed_value.bytes;

            if(bytes.size() != spec.byte_len
                || spec.byte_pos + spec.byte_len > headers_data.size())
            {
                CUSTOM_F_ERROR("custom tcp serialize error! byte_pos[%lu], byte_len[%lu]\n", spec.byte_pos, spec.byte_len);
                return std::nullopt;
            }

            std::copy(bytes.begin(), bytes.end(), headers_data.begin() + filed_value.spec.byte_pos);

        }

        std::vector<uint8_t> data;
        // 预留空间避免扩容
        data.reserve(headers_data.size() + body_data_.size());
        // 填充头部字段数据
        data.insert(data.end(), headers_data.begin(), headers_data.end());
        // 填充Body数据
        data.insert(data.end(), body_data_.begin(), body_data_.end());

        return data;
    }
    catch(const std::exception &e)
    {
        CUSTOM_F_ERROR("serialize exception: %s \n", e.what());
        return std::nullopt;
    }

}

std::string CustomTcpMessage::toString() const
{
    const auto &data = toBytes();
    if(!data.has_value())
    {
        return "";
    }
    return std::string(data->begin(), data->end());
}

bool CustomTcpMessage::writeHeadersHelper(std::vector<uint8_t> &headers_data) const
{
    for(auto &it : header_fields_by_byte_pos_)
    {
        const auto& field = it.second;
        const auto &field_spec = field.spec;
        switch(WriteKindOf(field_spec.role))
        {
            case FieldWriteKind::kZeroFill:
            {
                // 什么都不做保持填充0
                break;
            }
            case FieldWriteKind::kFixedMatch:
            {
                if(!field_spec.match.has_value() || !CustomTcpPattern::WriteAt(headers_data, field_spec, field_spec.match.value()))
                {
                    CUSTOM_F_ERROR("write match error! name[%s] pos[%ld] \n", field_spec.name.c_str(), field_spec.byte_pos);
                    return false;
                }

                break;
            }
            case FieldWriteKind::kItemFunctionCode:
            {
                const auto& bytes = CustomTcpPattern::ParseFromHex(field_spec, function_code_hex_);

                if(!CustomTcpPattern::WriteAt(headers_data, field_spec, bytes))
                {
                    CUSTOM_F_ERROR("write item function code error! name[%s] pos[%ld] \n", field_spec.name.c_str(), field_spec.byte_pos);
                    return false;
                }

                break;
            }
            case FieldWriteKind::kItemFieldOverride:
            {
                if(field.bytes.empty())
                {
                    CUSTOM_F_DEBUG("item override not set! name[%s] byte_pos[%ld] \n", field_spec.name.c_str(), field_spec.byte_pos);

                    break;
                }

                if(!CustomTcpPattern::WriteAt(headers_data, field_spec, field.bytes))
                {
                    CUSTOM_F_ERROR("write item override error! name[%s] byte_pos[%ld] \n", field_spec.name.c_str(), field_spec.byte_pos);
                    return false;
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
                CUSTOM_F_ERROR("write field unsupport! name[%s] byte_pos[%ld] role_tag[%s]\n", field_spec.name.c_str(), field_spec.byte_pos, RoleTag(field_spec.role).c_str());
                return false;
            }
            default:
                CUSTOM_F_ERROR("undefine write kind\n");
                return false;
        }
    }
    return true;
}


}
