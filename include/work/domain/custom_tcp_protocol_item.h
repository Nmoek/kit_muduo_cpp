/**
 * @file custom_tcp_protocol_item.h
 * @brief 自定义TCP测试协议项
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-12 19:00:09
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CUSTOM_TCP_PROTOCOL_ITEM_H__
#define __KIT_CUSTOM_TCP_PROTOCOL_ITEM_H__

#include "domain/protocol_item.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern_spec.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kit_domain {

class CustomTcpPattern;

struct CustomTcpItemCfg
{
    /// @brief 请求功能码十六进制值 H1234
    std::string function_code;
    /// @brief 按照byte_pos索引的用户配置的字段值
    std::unordered_map<size_t, std::vector<uint8_t>> field_values_by_byte_pos;

    CustomTcpItemCfg() = default;
    CustomTcpItemCfg(const nlohmann::json &tcp_json, const CustomTcpPatternSpec &spec);

    /**
     * @brief 通过json解析，允许部分更新
     * @param req_json 
     * @return true 
     * @return false 
     */
    bool fromJson(const nlohmann::json &tcp_json, const CustomTcpPatternSpec &spec);
};



class CustomTcpProtocolItem: public ProtocolItem
{
    
public:
    explicit CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> tcp_pattern);

    bool init(std::shared_ptr<Protocol> ori_protocol) override;

    bool setReqCfg(const nlohmann::json& req_json) override;
    bool setRespCfg(const nlohmann::json& resp_json) override;

    void setReqCfg(const CustomTcpItemCfg &req_cfg);
    void setRespCfg(const CustomTcpItemCfg &resp_cfg);

    ~CustomTcpProtocolItem() override = default;

    CustomTcpItemCfg getReqCfg() const { return req_cfg_; }
    CustomTcpItemCfg  getRespCfg() const { return resp_cfg_; }

private:
    /// @brief 自定义tcp格式(副本只存在于ProjectServe 所有ProtocolItem共享一个副本)
    std::weak_ptr<CustomTcpPattern> weak_tcp_pattern_;
    CustomTcpItemCfg req_cfg_;
    CustomTcpItemCfg resp_cfg_;
};



}
#endif //__KIT_CUSTOM_TCP_PROTOCOL_ITEM_H__
