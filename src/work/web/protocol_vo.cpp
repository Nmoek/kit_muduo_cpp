/**
 * @file project_vo.cpp
 * @brief  协议项 视图对象
 * @author ljk5
 * @version 1.0
 * @date 2025-07-24 20:06:50
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/protocol_vo.h"
#include "domain/protocol.h"
#include "domain/type.h"
#include "web/web_log.h"

using nljson = nlohmann::json;

namespace kit_domain {


static ProtocolCfgVoPtr CreateProtocolReqCfgVo(const ProtocolType type, const std::vector<char> &cfg_data)
{
    if(ProtocolType::HTTP_PROTOCOL == type ||
        ProtocolType::HTTPS_PROTOCOL == type)
    {
        auto p = std::make_shared<HttpProtocolReqCfgVo>();
        nljson::parse(cfg_data).get_to(*p);
        return p;
    }
    else if(ProtocolType::CUSTOM_TCP_PROTOCOL == type)
    {
        auto p = std::make_shared<TcpProtocolReqCfgVo>();
        nljson::parse(cfg_data).get_to(*p);
        return p;
    }

    throw std::invalid_argument("protocol type invalid!");
}

static ProtocolCfgVoPtr CreateProtocolRespCfgVo(const ProtocolType type, const std::vector<char> &cfg_data)
{
    if(ProtocolType::HTTP_PROTOCOL == type ||
        ProtocolType::HTTPS_PROTOCOL == type)
    {
        auto p = std::make_shared<HttpProtocolRespCfgVo>();
        nljson::parse(cfg_data).get_to(*p);
        return p;
    }
    else if(ProtocolType::CUSTOM_TCP_PROTOCOL == type)
    {
        auto p = std::make_shared<TcpProtocolRespCfgVo>();
        nljson::parse(cfg_data).get_to(*p);
        return p;
    }

    throw std::invalid_argument("protocol type invalid!");
}

ProtocolVo CovertProtocolVo(const Protocol &p)
{
    try {
        return ProtocolVo{
            p.m_id,
            p.m_name,
            ProtocolTypeToString(p.m_type),
            p.m_projectId,
            std::move(p.m_reqCfg),
            std::move(p.m_respCfg),
            ProtocolBodyTypeToString(p.m_reqBodyType),
            p.m_reqBodyDataStatus,
            ProtocolBodyTypeToString(p.m_respBodyType),
            p.m_respBodyDataStatus,
            p.m_ctime.toString(),
            p.m_utime.toString()
        };
    } catch(const std::exception& e) {

        PC_F_ERROR("CovertProtocolVo error! %s \n", e.what());
        
    }
    return ProtocolVo{};
}

std::vector<ProtocolVo> CovertProtocolVos(const std::vector<Protocol> &projects)
{
    std::vector<ProtocolVo> res;
    for(const auto &p : projects)
        res.emplace_back(CovertProtocolVo(p));
    return res;
}

}   // namespace kit_domain