/**
 * @file custom_tcp_pattern.c
 * @brief 自定义TCP协议格式
 * @author ljk5
 * @version 1.0
 * @date 2025-10-31 18:01:36
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_log.h"
#include "domain/type.h"

#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_pattern_field.h"
#include "net/net_data_converter.h"
#include "net/endian.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_protocol_item.h"


#include <assert.h>

using nljson = nlohmann::json;

namespace kit_domain {



std::shared_ptr<CustomTcpPatternFieldBase> CustomTcpPattern::checkField(std::shared_ptr<CustomTcpPatternFieldBase> cfg_field, const std::vector<char> &data)
{

    if(!cfg_field)
        return nullptr;

    // 根据起始位置 + 长度先读出 bytes数组
    std::vector<uint8_t> bytes(data.begin() + cfg_field ->byte_pos(), data.begin() + cfg_field->byte_len());

    // 大小端转换转换
    if(!KIT_IS_LOCAL_BIG_ENDIAN())
    {
        std::reverse(bytes.begin(), bytes.end());
    }

    // 克隆模式
    // 将这个bytes数组转换为实际的值
    auto real_field = cfg_field->clone();
    if(!real_field->fromBytes(bytes))
        return nullptr;

    // 注意：这里使用的抽象接口 值是取不出来的！
    // auto real_val = real_field->getValue();

    // 将解析出的值放到 字段列表中
    return real_field == cfg_field ? real_field : nullptr;
}

/***********BodyLengthDepPattern********* */


BodyLengthDepPattern::BodyLengthDepPattern(const std::vector<char>& info)
{

    try {

        nljson::parse(info).get_to<BodyLengthDepPattern>(*this);

    } catch (const std::exception& e) {

        CUSTOM_ERROR() << "BodyLengthDepPattern info parse error! " << std::endl << e.what() << std::endl;

        throw;
    }

}

int64_t BodyLengthDepPattern::calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field)
{
    // 必须已经提取过 实际数据
    assert(field);

    return kit_muduo::BytesToValue<int64_t>(field->toBytes(), !KIT_IS_LOCAL_BIG_ENDIAN());
}


std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> BodyLengthDepPattern::getSpecialFields() const
{
    std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> res;
    res.emplace_back(start_magic_num_field_);
    res.emplace_back(function_code_field_);
    res.emplace_back(body_length_field_);
    return res;
}

std::vector<char> BodyLengthDepPattern::serialize(const CustomTcpItemCfg& tcp_cfg, const std::vector<char> &body_data, bool is_endian)
{
    std::vector<char> data;

    // 组装Body长度进去
    auto it = tcp_cfg.headers.find(body_length_field_->byte_pos());

    if(it == tcp_cfg.headers.end())
    {
        return data;
    }
    auto real_body_length_field = (*it);

    // 本机变量转为大端
    real_body_length_field->setBytes(kit_muduo::ValueToBytes(body_data.size(), !KIT_IS_LOCAL_BIG_ENDIAN()));

    // 组装普通头部
    const auto& headers_data = tcp_cfg.assembleHeaders(is_endian);
    data.insert(data.end(), headers_data.begin(), headers_data.end());

    // 组装Body
    data.insert(data.end(), body_data.begin(), body_data.end());

    return data;
}



void to_json(nlohmann::json& root, const BodyLengthDepPattern& b)
{
    root = nljson::object();
    CUSTOM_WARN() << "BodyLengthDepPattern --> json not support" << std::endl;
}

void from_json(const nlohmann::json& root, BodyLengthDepPattern& b)
{
    b.least_byte_len_ = root["least_byte_len"];

    b.start_magic_num_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("start_magic_num_field"));
    b.start_magic_num_field_->setSepcial(true);

    b.function_code_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("function_code_field"));
    b.function_code_field_->setSepcial(true);

    b.body_length_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("body_length_field"));
    b.body_length_field_->setSepcial(true);

}


/***********BodyLengthDepPattern********* */


/************TotalLengthDepPattern************* */

TotalLengthDepPattern::TotalLengthDepPattern(const std::vector<char>& info)
{

    try {

        nljson::parse(info).get_to<TotalLengthDepPattern>(*this);

    }catch(const std::exception& e) {
        CUSTOM_ERROR() << "TotalLengthDepPattern info parse error! " << e.what() << std::endl;
        
        return;
    }

}



int64_t TotalLengthDepPattern::calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field)
{
    // 必须已经提取过 实际数据
    assert(field);

    // 报文总长度 - 头部长度
    return kit_muduo::BytesToValue<int64_t>(field->toBytes(), !KIT_IS_LOCAL_BIG_ENDIAN()) - least_byte_len_;
}


std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> TotalLengthDepPattern::getSpecialFields() const
{
    std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> res;
    res.emplace_back(start_magic_num_field_);
    res.emplace_back(function_code_field_);
    res.emplace_back(total_length_field_);
    return res;
}

std::vector<char> TotalLengthDepPattern::serialize(const CustomTcpItemCfg& tcp_cfg, const std::vector<char> &body_data, bool is_endian)
{
    std::vector<char> data;

    int64_t total_length = tcp_cfg.getHeaderBytes() + body_data.size();

    // 组装报文总长度进去
   auto it = tcp_cfg.headers.find(total_length_field_->byte_pos());

    if(it == tcp_cfg.headers.end())
    {
        return data;
    }
    auto real_total_length_field = (*it);
    
    // 本机变量转为大端
    real_total_length_field->setBytes(kit_muduo::ValueToBytes(total_length, !KIT_IS_LOCAL_BIG_ENDIAN()));
    

    // 组装普通头部
    const auto& headers_data = tcp_cfg.assembleHeaders(is_endian);
    data.insert(data.end(), headers_data.begin(), headers_data.end());

    // 组装Body
    data.insert(data.end(), body_data.begin(), body_data.end());

    assert(total_length == data.size());

    return data;
}


void to_json(nlohmann::json& root, const TotalLengthDepPattern& b)
{
    root = nljson::object();
    CUSTOM_WARN() << "BodyLengthDepPattern --> json not support" << std::endl;
}

void from_json(const nlohmann::json& root, TotalLengthDepPattern& b)
{
    b.least_byte_len_ = root["least_byte_len"];

    // b.head_filed_num_ = root["head_filed_num"];

    b.start_magic_num_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("start_magic_num_field"));
    b.start_magic_num_field_->setSepcial(true);

    b.function_code_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("function_code_field"));
    b.function_code_field_->setSepcial(true);


    b.total_length_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("total_length_field"));
    b.total_length_field_->setSepcial(true);
}
/************TotalLengthDepPattern************* */

/************NoLengthDepPattern************* */
NoLengthDepPattern::NoLengthDepPattern(const std::vector<char>& info)
{
    try {

        nljson::parse(info).get_to<NoLengthDepPattern>(*this);

    }catch(const std::exception& e) {
        CUSTOM_ERROR() << "NoLengthDepPattern info parse error! " << e.what() << std::endl;
        
        return;
    }

}

int64_t NoLengthDepPattern::calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field)
{
    return 0; // 视为没有body
}

std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> NoLengthDepPattern::getSpecialFields() const
{
    std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> res;
    res.emplace_back(start_magic_num_field_);
    res.emplace_back(function_code_field_);

    return res;
}

std::vector<char> NoLengthDepPattern::serialize(const CustomTcpItemCfg& tcp_cfg, const std::vector<char> &body_data, bool is_endian)
{
    std::vector<char> data;

    // 组装所有头部 只有头部没有Body
    const auto& headers_data = tcp_cfg.assembleHeaders(is_endian);
    data.insert(data.end(), headers_data.begin(), headers_data.end());

    assert(headers_data.size() == data.size());

    return data;
}

void to_json(nlohmann::json& root, const NoLengthDepPattern& b)
{
    root = nljson::object();
    CUSTOM_WARN() << "NoLengthDepPattern --> json not support" << std::endl;
}

void from_json(const nlohmann::json& root, NoLengthDepPattern& b)
{
    b.least_byte_len_ = root["least_byte_len"];

    b.start_magic_num_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("start_magic_num_field"));
    b.start_magic_num_field_->setSepcial(true);

    b.function_code_field_ = CustomTcpPatternFieldFactory::Create(root.at("special_fields").at("function_code_field"));
    b.function_code_field_->setSepcial(true);

    // b.end_magic_num_field_ = CustomTcpPatternFieldFactory::Create(root.at("end_magic_num_field"));
    // b.end_magic_num_field_->setSepcial(true);

}

/************NoLengthDepPattern************* */
std::shared_ptr<CustomTcpPattern> CustomTcpPatternFactory::Create(CustomTcpPatternType type, const nlohmann::json& info)
{
    if(CustomTcpPatternType::BODY_LENGTH_DEP == type)
    {
        return std::make_shared<BodyLengthDepPattern>(info);
    }
    else if(CustomTcpPatternType::TOTAL_LENGTH_DEP == type)
    {
        return std::make_shared<TotalLengthDepPattern>(info);
    }
    else if(CustomTcpPatternType::NO_LENGTH_DEP == type)
    {
        return std::make_shared<NoLengthDepPattern>(info);
    }

    return nullptr;
}




}