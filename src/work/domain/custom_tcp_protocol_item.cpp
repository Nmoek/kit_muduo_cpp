/**
 * @file custom_tcp_protocol_item.cpp
 * @brief 自定义TCP测试协议项
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-12 19:05:27
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_protocol_item.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/type.h"
#include "domain/protocol.h"

#include <stdexcept>


namespace kit_domain{
CustomTcpItemCfg::CustomTcpItemCfg(const nlohmann::json &tcp_json, std::shared_ptr<CustomTcpPattern> tcp_pattern)
{
    if(!fromJson(tcp_json, tcp_pattern))
    {
        throw std::runtime_error("tcp json parse error");
    }
}

CustomTcpItemCfg::CustomTcpItemCfg(std::shared_ptr<CustomTcpMessage> tcp_cfg)
{
    if(!fromNetCustomTcpReq(tcp_cfg))
    {
        throw std::runtime_error("net tcp cfg parse error");
    }
}

CustomTcpItemCfg CustomTcpItemCfg::clone()
{
    CustomTcpItemCfg cfg;
    cfg.function_code_filed_value = function_code_filed_value;
    for(auto &field : headers)
    {
        if(field)
        {
            cfg.headers.insert(field->clone());
        }
    }
    return cfg;
}

bool CustomTcpItemCfg::fromNetCustomTcpReq(std::shared_ptr<CustomTcpMessage> tcp_cfg)
{
    if(!tcp_cfg)
    {
        PC_F_ERROR("net tcp req is null\n");
        return false;
    }

    function_code_filed_value = tcp_cfg->functionCodeFieldValue();

    headers = tcp_cfg->headerFields();

    return true;
}


bool CustomTcpItemCfg::fromJson(const nlohmann::json &tcp_json,  std::shared_ptr<const CustomTcpPattern> tcp_pattern)
{
    if(tcp_json.empty() || !tcp_pattern)
    {
        PC_F_ERROR("json/pattern is null\n");
        return false;
    }
    auto it = tcp_json.find("common_fields");
    if(it == tcp_json.end())
    {
        PC_F_ERROR("json not found 'common_fields' field! \n");
        return false;
    }

    // 填充普通字段
    for(auto &obj : it.value())
    {
        auto cfg_field = CustomTcpPatternFieldFactory::Create(obj);
        if(!cfg_field)
        {
            PC_F_WARN("config field is null! %s\n", obj.dump().c_str());
            continue;
        }

        PC_F_ERROR("Req Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
            cfg_field->name().c_str(),cfg_field->idx(), cfg_field->byte_pos(), cfg_field->byte_len(), cfg_field->toHexString().c_str());
        

        cfg_field->setSepcial(false);
        auto p = headers.insert(cfg_field);
        if(!p.second) // 重叠区间直接返回错误
        {
            auto exist_field = *p.first;
            PC_F_ERROR("Req Field duplicated! exist: name[%d], pos[%d] <---> cur: name[%d], pos[%d]\n", exist_field->name().c_str(), exist_field->byte_pos(),cfg_field->name().c_str(), cfg_field->byte_pos());
            return false;
        }
    }
    

    // copy特殊字段并且填充
    for(auto &f : tcp_pattern->getSpecialFields())
    {
        auto special_field = f ? f->clone() : nullptr;
        if(!special_field)
        {
            PC_F_WARN("special field is null!\n");
            continue;
        }

        special_field->setSepcial(true);
        auto p = headers.insert(special_field);
        if(!p.second) // 重叠区间直接返回错误
        {
            auto exist_field = *p.first;
            PC_F_ERROR("Req Field duplicated! exist: name[%d], pos[%d] <---> cur: name[%d], pos[%d]\n", exist_field->name().c_str(), exist_field->byte_pos(),special_field->name().c_str(), special_field->byte_pos());
            return false;
        }
    }

    // 填充功能码字段
    it = tcp_json.find("function_code_filed_value");
    if(it == tcp_json.end())
    {
        PC_F_ERROR("json not dound 'function_code_filed_value' field! \n");
        return false;
    }
    function_code_filed_value = it.value();
    auto funcit = headers.find(tcp_pattern->functionCodeField()->byte_pos());
    if(funcit == headers.end())
    {
        PC_F_ERROR("function code field not found! \n");
        return false;
    }

    (*funcit)->fromHexString(it.value());

    PC_DEBUG() << "req function_code_filed_value: " << (*funcit)->toHexString() << std::endl;
    
    /** 调试打印 **/
    for(auto &field : headers)
    {
        if(field)
        {
            PC_F_ERROR("Req Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
                field->name().c_str(),field->idx(), field->byte_pos(), field->byte_len(), field->toHexString().c_str());
        }
        else
        {
            PC_F_ERROR("Field is null! \n");
        }
    }
    /** 调试打印 **/

    return true;
}

int64_t CustomTcpItemCfg::getHeaderBytes() const
{
    int64_t res = 0;
    for(auto &f : headers)
    {
        if(f)
        {
            res += f->byte_len();
        }
    }
    return res;
}


std::vector<uint8_t> CustomTcpItemCfg::assembleHeaders(bool is_endian) const
{
    std::vector<uint8_t> data;
    for(auto &it : headers)
    {
        std::vector<uint8_t> bytes = it->toBytes(is_endian);

        PC_F_DEBUG("serialize:: name[%s], idx[%d] byte_pos[%d], byte_len[%d], value[%s] ,Field extract success!\n", 
            it->name().c_str(),it->idx(), it->byte_pos(), it->byte_len(), kit_muduo::BytesToHexString(bytes).c_str());
        
        data.insert(data.end(), bytes.begin(), bytes.end());
    }
    return data;
}

CustomTcpProtocolItem::CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> tcp_pattern)
    :tcp_pattern_(tcp_pattern)
{

}




bool CustomTcpProtocolItem::init(std::shared_ptr<Protocol> ori_protocol)
{
    if(!ori_protocol || !tcp_pattern_)
    {
        PC_F_ERROR("ori protocol/ pattern data is null\n");
        return false;
    }

    const nljson &req_cfg_json = ori_protocol->m_reqCfg;

    const nljson &resp_cfg_json = ori_protocol->m_respCfg;
    
    /*注意：实践可以发现 只有从对端收到数据需要校验的那一边(收到请求边/收到响应边)才需要提前知道格式!
    
    只要是服务器回发处理的流程根本不需要格式, 只需要能够外部输入即可, 由用户自己保障 格式正确性 + 数据正确性即可!
    */

    if(!req_cfg_.fromJson(req_cfg_json, tcp_pattern_))
    {
        PC_F_ERROR("req cfg json parse error! %s\n", req_cfg_json.dump().c_str());
        return false;
    }

    req_body_view_.body_type = ProtocolBodyTypeToContentType(ori_protocol->m_reqBodyType);
    req_body_view_.body_data = std::make_shared<const std::vector<char>>(ori_protocol->m_reqBodyData);

    if(!resp_cfg_.fromJson(resp_cfg_json, tcp_pattern_))
    {
        PC_F_ERROR("resp cfg json parse error! %s\n", resp_cfg_json.dump().c_str());
        return false;
    }

    resp_body_view_.body_type = ProtocolBodyTypeToContentType(ori_protocol->m_respBodyType);
    resp_body_view_.body_data = std::make_shared<const std::vector<char>>(ori_protocol->m_respBodyData);

    return true;
}

bool CustomTcpProtocolItem::setReqCfg(const nlohmann::json& tcp_json)
{
    return req_cfg_.fromJson(tcp_json, tcp_pattern_);
}

bool CustomTcpProtocolItem::setRespCfg(const nlohmann::json& tcp_json)
{
    return resp_cfg_.fromJson(tcp_json, tcp_pattern_);
}

void CustomTcpProtocolItem::setReqCfg(const CustomTcpItemCfg &req_cfg)
{
    req_cfg_ = req_cfg;
}

void CustomTcpProtocolItem::setRespCfg(const CustomTcpItemCfg &resp_cfg)
{
    resp_cfg_ = resp_cfg;
}



}