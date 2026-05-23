/**
 * @file custom_tcp_message.cpp
 * @brief 自定义TCP消息(请求/响应一体)
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 18:51:40
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/domain_log.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_context.h"


namespace kit_domain {



CustomTcpMessage::CustomTcpMessage()
    :recordTime_(0)
{


    CUSTOM_F_DEBUG("CustomTcpRequest::construct() %p\n", this);
}

CustomTcpMessage::~CustomTcpMessage()
{
    CUSTOM_F_DEBUG("CustomTcpMessage::~CustomTcpMessage() %p\n", this);
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



uint64_t CustomTcpMessage::getHeaderBytes() const
{
    uint64_t res = 0;
    for(auto &it : header_fields_by_byte_pos_)
    {
        res += it.second.spec.byte_len;
    }
    return res;
}




}
