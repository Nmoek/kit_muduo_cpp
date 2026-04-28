/**
 * @file svc_protocol.cpp
 * @brief 测试协议项 servie层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-25 18:17:12
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/protocol.h"
#include "service/svc_protocol.h"
#include "repository/repo_protocol.h"
#include "service/svc_log.h"

namespace kit_domain {
ProtocolService::ProtocolService(std::shared_ptr<ProtocolRepoInterface> repo)
    :ProtocolSvcInterface(repo)
{

}

ProtocolService::~ProtocolService()
{

}

int64_t ProtocolService::Add(kit_muduo::HttpContextPtr ctx, Protocol &domainPc)
{
    return _repo->Create(ctx, domainPc);
}
bool ProtocolService::Del(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    return _repo->UpdateStatusById(ctx, protocol_id, static_cast<int32_t>(ProtocolStatus::INACTIVE));
}

bool ProtocolService::ReCover(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    return _repo->UpdateStatusById(ctx, protocol_id, static_cast<int32_t>(ProtocolStatus::ACTIVE));
}

bool ProtocolService::UpdateById(kit_muduo::HttpContextPtr ctx, Protocol &domainPc)
{
    return _repo->UpdateById(ctx, domainPc);
}

bool ProtocolService::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& name)
{
    return _repo->UpdateName(ctx, protocol_id, name);
}


bool ProtocolService::UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const std::string& cfg_data)
{
    return _repo->UpdateCfg(ctx, protocol_id, req_or_resp, cfg_data);
}

bool ProtocolService::UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json)
{
    return _repo->UpdateCfg(ctx, protocol_id, req_or_resp, cfg_json);
}

bool ProtocolService::UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t body_type, const std::vector<char>& cfg_data)
{
    return _repo->UpdateBody(ctx, protocol_id, req_or_resp, body_type, cfg_data);
}

Protocol ProtocolService::GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    return _repo->GetById(ctx, protocol_id);
}

std::vector<Protocol> ProtocolService::GetByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, int32_t offset, int32_t limit)
{
    return _repo->GetByProject(ctx, projectId, offset, limit);
}

std::vector<Protocol> ProtocolService::GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) 
{
    return _repo->GetActiveByProject(ctx, project_id);
}

int32_t ProtocolService::GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return _repo->GetProtocolCnt(ctx, project_id);
}

nlohmann::json ProtocolService::GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp)
{
    return _repo->GetTcpCommonFieldsById(ctx, protocol_id, req_or_resp);
}


ProtocolBodyType ProtocolService::GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp)
{
    return _repo->GetBodyTypeById(ctx, protocol_id, req_or_resp);
}

bool ProtocolService::GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data)
{
    return _repo->GetBodyDataById(ctx, protocol_id, req_or_resp, body_data);
}

bool ProtocolService::GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, ProtocolBodyType &body_type, std::vector<char> &body_data)
{
    return _repo->GetBodyInfoById(ctx, protocol_id, req_or_resp, body_type, body_data);

}


nlohmann::json ProtocolService::GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    return _repo->GetCfgById(ctx, protocol_id);
}

}   // namespace kit_domain 