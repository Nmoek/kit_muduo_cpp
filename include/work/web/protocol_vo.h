/**
 * @file protocol_vo.h
 * @brief 协议项 视图对象
 * @author ljk5
 * @version 1.0
 * @date 2025-07-30 16:28:24
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_PROTOCOL_VO_H__
#define __KIT_PROTOCOL_VO_H__

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace kit_domain {

class Protocol;

struct ProtocolVo
{
    int64_t                 id;            // 协议主键Id
    std::string             name;          // 测试协议名称
    std::string             type;          // 测试协议类型
    int64_t                 project_id;    // 所属测试服务Id
    int32_t                 status;        // 协议项状态：1 active，2 inactive
    int32_t                 config_state; // 协议项配置状态 0未上线  1已上线 2待重配置

    nlohmann::json          req_cfg;  // 请求配置项
    nlohmann::json          resp_cfg;  // 请求配置项

    std::string             req_body_type;  // 请求协议数据类型
    int32_t                 req_body_status;  // 请求协议数据状态

    std::string             resp_body_type; // 响应协议数据类型
    int32_t                 resp_body_status; // 响应协议数据状态

    std::string             ctime;        // 创建时间
    std::string             utime;        // 更新时间

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ProtocolVo, id, name, type, project_id, status, config_state, req_cfg, resp_cfg, req_body_type, req_body_status, resp_body_type, resp_body_status, ctime, utime)
};


ProtocolVo CovertProtocolVo(const Protocol &p);

std::vector<ProtocolVo> CovertProtocolVos(const std::vector<Protocol> &projects);





}   //namespace kit_domain
#endif  //__KIT_PROTOCOL_VO_H__
