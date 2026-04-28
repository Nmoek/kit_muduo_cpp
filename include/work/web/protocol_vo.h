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

struct ProtocolCfgVo {
    virtual ~ProtocolCfgVo() = default;

    virtual nlohmann::json toJson() = 0;
};

using ProtocolCfgVoPtr = std::shared_ptr<ProtocolCfgVo>;


struct HttpProtocolReqCfgVo :public ProtocolCfgVo{
    std::string method;        // 请求方法
    std::string path;          // 请求路径

    nlohmann::json toJson() override
    {
        return {
            {"method", this->method},
            {"path", this->path},
        };
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HttpProtocolReqCfgVo, method, path)

};

struct HttpProtocolRespCfgVo :public ProtocolCfgVo{
    int32_t status_code;    //响应状态码

    nlohmann::json toJson() override
    {
        return {
            {"status_code", this->status_code},
        };
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HttpProtocolRespCfgVo, status_code)
};

struct TcpProtocolReqCfgVo :public ProtocolCfgVo{
    std::string function_code_filed_value;  // 功能码

    nlohmann::json toJson() override
    {
        return {
            {"function_code_filed_value", function_code_filed_value}
        };
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TcpProtocolReqCfgVo, function_code_filed_value)
};

struct TcpProtocolRespCfgVo :public ProtocolCfgVo{
    std::string function_code_filed_value;  // 功能码

    nlohmann::json toJson() override
    {
        return {
            {"function_code_filed_value", function_code_filed_value}
        };
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TcpProtocolRespCfgVo, function_code_filed_value)
};

struct ProtocolVo
{
    int64_t                 id;            // 协议主键Id
    std::string             name;          // 测试协议名称
    std::string             type;          // 测试协议类型
    int64_t                 project_id;    // 所属测试服务Id

    // 思考: 这里需要抽象并解析出数据吗? 唯一需要解析的地方是生成ProtocolItem的地方

    // ProtocolCfgVoPtr        req_cfg;  // 请求配置项
    // ProtocolCfgVoPtr        resp_cfg;  // 请求配置项
    nlohmann::json          req_cfg;  // 请求配置项
    nlohmann::json          resp_cfg;  // 请求配置项

    std::string             req_body_type;  // 请求协议数据类型
    int32_t                 req_body_status;  // 请求协议数据状态

    std::string             resp_body_type; // 响应协议数据类型
    int32_t                 resp_body_status; // 响应协议数据状态

    std::string             ctime;        // 创建时间
    std::string             utime;        // 更新时间

    //TODO: 统计数据 交互次数 成功/错误比例等

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ProtocolVo, id, name, type, project_id, req_cfg, resp_cfg, req_body_type, req_body_status, resp_body_type, resp_body_status, ctime, utime)
};


ProtocolVo CovertProtocolVo(const Protocol &p);

std::vector<ProtocolVo> CovertProtocolVos(const std::vector<Protocol> &projects);





}   //namespace kit_domain
#endif  //__KIT_PROTOCOL_VO_H__