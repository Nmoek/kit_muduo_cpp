/**
 * @file repo_protocol.cpp
 * @brief 协议项 repo层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-29 16:20:51
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "repository/repo_protocol.h"
#include "dao/dao_protocol.h"

#include "domain/type.h"
#include "repository/repo_log.h"
#include "domain/protocol.h"
#include "dao/protocol.h"
#include "base/time_stamp.h"

using nljson = nlohmann::json;
using namespace kit_dao;

namespace kit_domain {

static kit_domain::Protocol CovertDomainProtocol(const kit_dao::Protocol &daoPj)
{
    return kit_domain::Protocol{
        .m_id = daoPj.m_id,
        .m_name = daoPj.m_name,
        .m_type = static_cast<ProtocolType>(daoPj.m_type),
        .m_projectId = daoPj.m_projectId,
        .m_status = static_cast<ProtocolStatus>(daoPj.m_status),
        .m_reqBodyType= static_cast<ProtocolBodyType>(daoPj.m_reqBodyType),
        .m_respBodyType = static_cast<ProtocolBodyType>(daoPj.m_respBodyType),
        .m_reqBodyDataStatus = daoPj.m_reqBodyDataStatus,
        .m_respBodyDataStatus = daoPj.m_respBodyDataStatus,
        .m_reqCfg = nljson::parse(daoPj.m_reqCfg),
        .m_respCfg = nljson::parse(daoPj.m_respCfg),
        .m_reqBodyData = std::move(daoPj.m_reqBodyData),
        .m_respBodyData = std::move(daoPj.m_respBodyData),
        .m_isEndian = static_cast<bool>(daoPj.m_isEndian),

        .m_ctime = kit_muduo::TimeStamp(daoPj.m_ctime),
        .m_utime = kit_muduo::TimeStamp(daoPj.m_utime)
    };
}



static std::vector<kit_domain::Protocol> CovertDomainProtocols(const std::vector<kit_dao::Protocol> &daoPjs)
{
    std::vector<kit_domain::Protocol> ans;
    for(const auto &p : daoPjs)
    {
        ans.emplace_back(CovertDomainProtocol(p));
    }
    return ans;
}

static kit_dao::Protocol CovertDaoProtocol(const kit_domain::Protocol &domainPc)
{
    return kit_dao::Protocol {
        domainPc.m_id,
        domainPc.m_name,
        static_cast<int32_t>(domainPc.m_type),
        domainPc.m_projectId,
        static_cast<int32_t>(domainPc.m_status),
        static_cast<int32_t>(domainPc.m_reqBodyType),
        static_cast<int32_t>(domainPc.m_respBodyType),
        domainPc.m_reqBodyDataStatus,
        domainPc.m_respBodyDataStatus,
        domainPc.m_reqCfg.dump(),
        domainPc.m_respCfg.dump(),
        std::move(domainPc.m_reqBodyData),
        std::move(domainPc.m_respBodyData),
        static_cast<int32_t>(domainPc.m_isEndian),
    };
}

ProtocolRepository::ProtocolRepository(std::shared_ptr<ProtocolDaoInterface> dao)
    :ProtocolRepoInterface(dao)
{

}

ProtocolRepository::~ProtocolRepository() { }


int64_t ProtocolRepository::Create(kit_muduo::HttpContextPtr ctx, Protocol &domainPc)
{
    return _dao->Insert(ctx, CovertDaoProtocol(domainPc));
}


bool ProtocolRepository::UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t status)
{
    return _dao->UpdateStatusById(ctx, protocolId, status);
}

bool ProtocolRepository::UpdateById(kit_muduo::HttpContextPtr ctx, Protocol &domainPc)
{
    return _dao->UpdateById(ctx, CovertDaoProtocol(domainPc));
}

bool ProtocolRepository::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocolId, const std::string &name)
{
    return _dao->UpdateName(ctx, protocolId, name);
}

bool ProtocolRepository::UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t req_or_resp, const std::string& cfg_data)
{
    return _dao->UpdateCfg(ctx, protocolId, req_or_resp, cfg_data);
}

bool ProtocolRepository::UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json)
{
    return _dao->UpdateCfg(ctx, protocol_id, req_or_resp, cfg_json);
}

bool ProtocolRepository::UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t req_or_resp, int32_t body_type, const std::vector<char>& cfg_data)
{
    return _dao->UpdateBody(ctx, protocolId, req_or_resp, body_type, cfg_data);
}

Protocol ProtocolRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t protocolId)
{
    return CovertDomainProtocol(_dao->GetById(ctx, protocolId));
}

std::vector<Protocol> ProtocolRepository::GetByProject(kit_muduo::HttpContextPtr ctx, int64_t protocolId, ProtocolStatus status, int32_t offset, int32_t limit)
{
    return CovertDomainProtocols(_dao->GetByProject(ctx, protocolId, static_cast<int32_t>(status), offset, limit));
}

std::vector<Protocol> ProtocolRepository::GetAllByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, ProtocolStatus status)
{
    return CovertDomainProtocols(_dao->GetAllByProject(ctx, project_id, static_cast<int32_t>(status)));
}


int32_t ProtocolRepository::GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t project_id, ProtocolStatus status)
{
    return _dao->CountByProject(ctx, project_id, static_cast<int32_t>(status));
}

nlohmann::json ProtocolRepository::GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp)
{
    return nljson::parse(_dao->GetTcpCommonFieldsById(ctx, protocol_id, req_or_resp));
}


kit_domain::ProtocolBodyType ProtocolRepository::GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp)
{
    return static_cast<ProtocolBodyType>(_dao->GetBodyTypeById(ctx, protocol_id, req_or_resp));
}

bool ProtocolRepository::GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data)
{
    return _dao->GetBodyDataById(ctx, protocol_id, req_or_resp, body_data);
}

bool ProtocolRepository::GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, ProtocolBodyType &body_type, std::vector<char> &body_data)
{
    // 查询两次 组装
    // 查一次 两个字段一起
    int32_t dao_body_type;
    bool ok = _dao->GetBodyInfoById(ctx, protocol_id, req_or_resp, dao_body_type, body_data);

    if(!ok) {
        return false;
    }

    body_type = static_cast<ProtocolBodyType>(dao_body_type);
    
    return true;
}


nlohmann::json ProtocolRepository::GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    return _dao->GetCfgById(ctx, protocol_id);
}

} // kit_domain