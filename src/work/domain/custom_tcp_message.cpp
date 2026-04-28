/**
 * @file custom_tcp_message.cpp
 * @brief 自定义TCP消息(请求/响应一体)
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 18:51:40
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_log.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_context.h"


namespace kit_domain {



CustomTcpMessage::CustomTcpMessage()
    :recordTime_(0)
{


    PJ_F_DEBUG("CustomTcpRequest::construct() %p\n", this);
}

CustomTcpMessage::CustomTcpMessage(int32_t header_field_num)
    :header_fileds_(header_field_num, nullptr)
    ,recordTime_(0)
{

    PJ_F_DEBUG("CustomTcpRequest::construct() %p\n", this);
}

CustomTcpMessage::~CustomTcpMessage()
{
    PJ_F_DEBUG("CustomTcpMessage::~CustomTcpMessage() %p\n", this);
}

void CustomTcpMessage::addField(int32_t idx, std::shared_ptr<CustomTcpPatternFieldBase> field)
{
    // if(idx < 0 || idx >= header_fileds_.size() || header_fileds_[idx] != nullptr)
    //     return;

    header_fileds_.at(idx) = field;
}

std::shared_ptr<CustomTcpPatternFieldBase> CustomTcpMessage::getField(int32_t idx) const
{
    // if(idx < 0 || idx >= header_fileds_.size())
    //     return nullptr;
    return header_fileds_.at(idx);
}


int64_t CustomTcpMessage::getHeaderBytes() const
{
    int64_t res = 0;
    for(auto &f : header_fileds_)
    {
        if(f)
            res += f->byte_len();
    }
    return res;
}



void from_json(const nlohmann::json& root, CustomTcpMessage& message)
{
    message.function_code_filed_value_ = root["function_code_filed_value"];

    for(auto &obj : root["common_fields"])
    {

        auto cfg_field = CustomTcpPatternFieldFactory::Create(obj);
        if(!cfg_field)
        {
            CUSTOM_ERROR() << "create field failed! \n" << obj.dump(4) << std::endl;

            throw std::invalid_argument("pattern_fields invalid!");
        }
        cfg_field->setSepcial(false);
        message.addField(cfg_field->idx(), cfg_field);
    }
    
}


}