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

namespace {

/**
 * @brief 生成协议项唯一运行键值
 * @param type
 * @param req_cfg
 * @return std::string
 */
std::string GenerateRuntimeKey(ProtocolType type, nlohmann::json req_cfg)
{
    std::string key;
    if(ProtocolType::kHttp == type)
    {
        key += "|";
        key += "HTTP";
        key += "|";
        auto it = req_cfg.find("method");
        if(it == req_cfg.end())
        {
            return "";
        }
        key += it.value().get<std::string>();
        key += "|";
        it = req_cfg.find("path");
        if(it == req_cfg.end())
        {
            return "";
        }
        key += it.value().get<std::string>();
        key += "|";
    }
    else if(ProtocolType::kCustomTcp == type)
    {
        key += "|";
        key += "CUTOM_TCP";
        key += "|";
        auto it = req_cfg.find("function_code");
        if(it == req_cfg.end())
        {
            return "";
        }
        key += it.value().get<std::string>();
        key += "|";
    }
    return key;
}



static kit_domain::Protocol CovertDomainProtocol(const kit_dao::Protocol &daoPj)
{
    return kit_domain::Protocol{
        .m_id = daoPj.m_id,
        .m_name = daoPj.m_name,
        .m_type = static_cast<ProtocolType>(daoPj.m_type),
        .m_projectId = daoPj.m_projectId,
        .m_runtimeKey = daoPj.m_runtimeKey,
        .m_status = static_cast<ProtocolStatus>(daoPj.m_status),
        .m_configState = static_cast<ProtocolConfigState>(daoPj.m_configState),
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

static kit_dao::Protocol CovertDaoProtocol(const std::string& runtime_key, const kit_domain::Protocol &domainPc)
{
    return kit_dao::Protocol {
        domainPc.m_id,
        domainPc.m_name,
        static_cast<int32_t>(domainPc.m_type),
        domainPc.m_projectId,
        runtime_key,
        static_cast<int32_t>(domainPc.m_status),
        static_cast<int32_t>(domainPc.m_configState),
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

static kit_domain::ProtocolAccessInfo CovertDomainProtocolAccessInfo(const kit_dao::ProtocolAccessInfo &p)
{
    return kit_domain::ProtocolAccessInfo{
        .protocol_id = p.protocol_id,
        .project_id = p.project_id,
        .runtime_key = std::move(p.runtime_key),
        .protocol_type = static_cast<ProtocolType>(p.protocol_type),
        .protocol_status = static_cast<ProtocolStatus>(p.protocol_status),
        .protocol_config_state = static_cast<ProtocolConfigState>(p.protocol_config_state),
        .project_user_id = p.project_user_id,
        .project_runtime_state = static_cast<ProjectRuntimeState>(p.project_runtime_state),
        .project_status = static_cast<ProjectStatus>(p.project_status)

    };
}

}

ProtocolRepository::ProtocolRepository(std::shared_ptr<ProtocolDaoInterface> dao)
    :ProtocolRepoInterface(dao)
{

}

ProtocolRepository::~ProtocolRepository() { }


int64_t ProtocolRepository::Create(kit_muduo::HttpContextPtr ctx, Protocol &domainPc)
{
    const std::string& runtime_key = GenerateRuntimeKey(domainPc.m_type, domainPc.m_reqCfg);
    if(runtime_key.empty())
    {
        REPOPC_F_ERROR("rutime key generate error! type[%d]: %s\n", static_cast<int32_t>(domainPc.m_type), domainPc.m_reqCfg.dump().c_str());
        return -1;
    }
    return _dao->Insert(ctx, CovertDaoProtocol(runtime_key, domainPc));
}


bool ProtocolRepository::UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t status)
{
    return _dao->UpdateStatusById(ctx, protocolId, status);
}

bool ProtocolRepository::UpdateById(kit_muduo::HttpContextPtr ctx, Protocol &domainPc)
{
    const std::string& runtime_key = GenerateRuntimeKey(domainPc.m_type, domainPc.m_reqCfg);
    if(runtime_key.empty())
    {
        REPOPC_F_ERROR("rutime key generate error! type[%d]: %s\n", static_cast<int32_t>(domainPc.m_type), domainPc.m_reqCfg.dump().c_str());
        return false;
    }
    return _dao->UpdateById(ctx, CovertDaoProtocol(runtime_key, domainPc));
}

bool ProtocolRepository::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocolId, const std::string &name)
{
    return _dao->UpdateName(ctx, protocolId, name);
}

bool ProtocolRepository::UpdateReqCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json)
{
    const std::string &runtime_key = GenerateRuntimeKey(type, cfg_json);
    if(runtime_key.empty())
    {
        REPOPC_F_ERROR("runtime key generate error! type[%d]: %s\n", static_cast<int32_t>(type), cfg_json.dump().c_str());
        return false;
    }

    return _dao->UpdateReqCfg(ctx, protocol_id, runtime_key, cfg_json);
}

bool ProtocolRepository::UpdateRespCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json)
{
    return _dao->UpdateRespCfg(ctx, protocol_id, cfg_json);
}

bool ProtocolRepository::UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocolId, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char>& cfg_data)
{
    return _dao->UpdateBody(ctx, protocolId, static_cast<int32_t>(side), static_cast<int32_t>(body_type), cfg_data);
}

Protocol ProtocolRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t protocolId)
{
    return CovertDomainProtocol(_dao->GetById(ctx, protocolId));
}

std::vector<Protocol> ProtocolRepository::GetByProject(kit_muduo::HttpContextPtr ctx, int64_t protocolId, ProtocolStatus status, int32_t offset, int32_t limit)
{
    return CovertDomainProtocols(_dao->ListByProject(ctx, protocolId, static_cast<int32_t>(status), offset, limit));
}

std::vector<Protocol> ProtocolRepository::GetValidByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return CovertDomainProtocols(_dao->GetAll(ctx, project_id, static_cast<int32_t>(ProtocolStatus::kValid), -1));
}

std::vector<Protocol> ProtocolRepository::GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return CovertDomainProtocols(_dao->GetAll(ctx, project_id, static_cast<int32_t>(ProtocolStatus::kValid), static_cast<int32_t>(ProtocolConfigState::kOn)));
}

int32_t ProtocolRepository::GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t project_id, ProtocolStatus status)
{
    return _dao->CountByProject(ctx, project_id, static_cast<int32_t>(status));
}

nlohmann::json ProtocolRepository::GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side)
{
    return nljson::parse(_dao->GetTcpCommonFieldsById(ctx, protocol_id, static_cast<int32_t>(side)));
}


kit_domain::ProtocolBodyType ProtocolRepository::GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side)
{
    return static_cast<ProtocolBodyType>(_dao->GetBodyTypeById(ctx, protocol_id, static_cast<int32_t>(side)));
}

bool ProtocolRepository::GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, std::vector<char> &body_data)
{
    return _dao->GetBodyDataById(ctx, protocol_id, static_cast<int32_t>(side), body_data);
}

bool ProtocolRepository::GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType &body_type, std::vector<char> &body_data)
{
    // 查询两次 组装
    // 查一次 两个字段一起
    int32_t dao_body_type;
    bool ok = _dao->GetBodyInfoById(ctx, protocol_id, static_cast<int32_t>(side), dao_body_type, body_data);

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

bool ProtocolRepository::GetAccessInfo(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolAccessInfo& access_info)
{
    auto access_opt = _dao->AccessProtocolAndProjectByJoin(ctx, protocol_id);
    if(!access_opt.has_value())
    {
        return false;
    }
    access_info = CovertDomainProtocolAccessInfo(access_opt.value());
    return true;
}

bool ProtocolRepository::UpdateConfigState(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolConfigState config_state)
{
    return _dao->UpdateConfigState(ctx, protocol_id, static_cast<int32_t>(config_state));
}

} // kit_domain