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
#include "domain/domain_log.h"
#include "net/net_data_converter.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>


namespace kit_domain{

namespace {

bool IsExactHexBytes(const std::string& hex, size_t byte_len)
{
    if(hex.size() != byte_len * 2 + 1 || hex.empty() || hex[0] != 'H')
    {
        return false;
    }

    return std::all_of(hex.begin() + 1, hex.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool ParseBytePos(const std::string& text, size_t& byte_pos)
{
    if(text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    }))
    {
        return false;
    }

    try
    {
        byte_pos = static_cast<size_t>(std::stoull(text));
        return true;
    }
    catch(const std::exception&)
    {
        return false;
    }
}

} // namespace

CustomTcpItemCfg::CustomTcpItemCfg(const nlohmann::json &tcp_json, const CustomTcpPatternSpec &spec)
{
    if(!fromJson(tcp_json, spec))
    {
        throw std::runtime_error("tcp json parse error");
    }
}


bool CustomTcpItemCfg::fromJson(const nlohmann::json &tcp_json,  const CustomTcpPatternSpec &spec)
{
    if(tcp_json.empty())
    {
        PCITEM_F_ERROR("json/pattern is null\n");
        return false;
    }

    const FieldSpec* function_field = spec.byUniqueRole(FieldRole::kFunctionCode);
    if(!function_field)
    {
        PCITEM_F_ERROR("pattern 'function_code' role not found\n");
        return false;
    }

    CustomTcpItemCfg parsed;
    // 填充功能码字段
    auto it = tcp_json.find("function_code");
    if(it == tcp_json.end()
        || !it->is_string())
    {
        PCITEM_F_ERROR("field 'function_code' invalid! \n");
        return false;
    }
    it.value().get_to<std::string>(parsed.function_code);

    if(!IsExactHexBytes(parsed.function_code, function_field->byte_len))
    {
        PCITEM_F_ERROR("json field 'function_code' value invalid: %s\n", parsed.function_code.c_str());
        return false;
    }

    PCITEM_DEBUG() << "req function_code: " << parsed.function_code << std::endl;

    // 填充字段值
    it = tcp_json.find("fields");
    if(it == tcp_json.end() || !it->is_object())
    {
        PCITEM_F_ERROR("json field 'fields' not found or not object! \n");
        return false;
    }

    for(auto &obj : it.value().items())
    {
        size_t byte_pos = 0;
        if(!ParseBytePos(obj.key(), byte_pos))
        {
            PCITEM_F_ERROR("Req Field Value: byte_pos key invalid [%s]\n", obj.key().c_str());
            return false;
        }

        const FieldSpec *field = spec.byPos(byte_pos);
        if(!field)
        {
            PCITEM_F_ERROR("Req Field Value: byte_pos[%ld] not found \n", byte_pos);
            return false;
        }
        if(!IsItemOverrideAllowed(field->role))
        {
            PCITEM_F_ERROR("Req Field Value: byte_pos[%ld] role[%s] cannot override \n", byte_pos, RoleTag(field->role).c_str());
            return false;
        }
        if(!obj.value().is_string())
        {
            PCITEM_F_ERROR("Req Field Value: byte_pos[%ld] value type invalid\n", byte_pos);
            return false;
        }
        const std::string &value_hex = obj.value().get<std::string>();
        if(!IsExactHexBytes(value_hex, field->byte_len))
        {
            PCITEM_F_ERROR("item cfg hex invalid, byte_pos[%ld], byte_len[%ld], value[%s]\n", byte_pos, field->byte_len, value_hex.c_str());
            return false;
        }
        // TODO: 明确 ProtocolItem::isEndian() 的业务语义后，在协议项配置进入
        // wire bytes 的边界统一处理端序；当前十六进制值按用户给出的原始字节保存。
        auto value_bytes = kit_muduo::HexStringToBytes(value_hex);
        parsed.field_values_by_byte_pos[byte_pos] = std::move(value_bytes);

        PCITEM_F_DEBUG("Req Field Value: name[%s], byte_pos[%d], byte_len[%d], value[%s]\n",
            field->name.c_str(),field->byte_pos, field->byte_len, value_hex.c_str());
    }

    *this = std::move(parsed);
    return true;
}



CustomTcpProtocolItem::CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> tcp_pattern)
    :weak_tcp_pattern_(tcp_pattern)
{

}

bool CustomTcpProtocolItem::init(std::shared_ptr<Protocol> ori_protocol)
{
    auto tcp_pattern = weak_tcp_pattern_.lock();
    if(!ori_protocol || !tcp_pattern)
    {
        PCITEM_F_ERROR("ori protocol/ pattern data is null\n");
        return false;
    }
    const auto& spec = tcp_pattern->spec();

    initBase(*ori_protocol);

    const nljson &req_cfg_json = ori_protocol->m_reqCfg;

    const nljson &resp_cfg_json = ori_protocol->m_respCfg;

    /*注意：实践可以发现 只有从对端收到数据需要校验的那一边(收到请求边/收到响应边)才需要提前知道格式!

    只要是服务器回发处理的流程根本不需要格式, 只需要能够外部输入即可, 由用户自己保障 格式正确性 + 数据正确性即可!
    */

    if(!req_cfg_.fromJson(req_cfg_json, spec))
    {
        PCITEM_F_ERROR("req cfg json parse error! %s\n", req_cfg_json.dump().c_str());
        return false;
    }

    if(!resp_cfg_.fromJson(resp_cfg_json, spec))
    {
        PCITEM_F_ERROR("resp cfg json parse error! %s\n", resp_cfg_json.dump().c_str());
        return false;
    }

    return true;
}

bool CustomTcpProtocolItem::setReqCfg(const nlohmann::json& tcp_json)
{
    auto tcp_pattern = weak_tcp_pattern_.lock();
    if(tcp_json.empty() || !tcp_pattern)
    {
        PCITEM_F_ERROR("ori protocol/ pattern data is null\n");
        return false;
    }

    return req_cfg_.fromJson(tcp_json, tcp_pattern->spec());
}

bool CustomTcpProtocolItem::setRespCfg(const nlohmann::json& tcp_json)
{
    auto tcp_pattern = weak_tcp_pattern_.lock();
    if(tcp_json.empty() || !tcp_pattern)
    {
        PCITEM_F_ERROR("ori protocol/ pattern data is null\n");
        return false;
    }
    return resp_cfg_.fromJson(tcp_json, tcp_pattern->spec());
}

void CustomTcpProtocolItem::init(const Protocol& ori_protocol,
    const CustomTcpItemCfg& req_cfg,
    const CustomTcpItemCfg& resp_cfg)
{
    initBase(ori_protocol);
    req_cfg_ = req_cfg;
    resp_cfg_ = resp_cfg;
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
