/**
 * @file repo_project.cpp
 * @brief 测试服务 repo层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-22 15:40:30
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "repository/repo_project.h"
#include "dao/dao_project.h"
#include "domain/type.h"
#include "repository/repo_log.h"
#include "domain/project.h"
#include "base/time_stamp.h"


using namespace kit_dao;

namespace kit_domain {
ProjectRepository::ProjectRepository(std::shared_ptr<ProjectDaoInterface> dao)
    :ProjectRepoInterface(dao)
{

}

ProjectRepository::~ProjectRepository() { }

static nlohmann::json CovertPatternInfoJson(const std::vector<char> &pattern_info)
{
    if(pattern_info.empty())
    {
        return nlohmann::json::object();
    }

    const std::string pattern_info_text(pattern_info.begin(), pattern_info.end());
    nlohmann::json pattern_info_json = nlohmann::json::parse(pattern_info_text, nullptr, false);
    return pattern_info_json.is_discarded() ? nlohmann::json::object() : pattern_info_json;
}

static std::vector<char> CovertPatternInfoBytes(const nlohmann::json &pattern_info)
{
    const std::string pattern_info_text = pattern_info.dump();
    return std::vector<char>(pattern_info_text.begin(), pattern_info_text.end());
}

static kit_domain::Project CovertDomainProject(const kit_dao::Project &daoPj)
{
    return kit_domain::Project{
        daoPj.m_id,
        daoPj.m_name,
        static_cast<ProjectMode>(daoPj.m_mode),
        static_cast<ProtocolType>(daoPj.m_protocolType),
        daoPj.m_listenPort,
        daoPj.m_targetIp,
        daoPj.m_userId,
        static_cast<ProjectStatus>(daoPj.m_status),
        static_cast<ProjectRuntimeState>(daoPj.m_runtimeState),
        CovertPatternInfoJson(daoPj.m_patternInfo),
        kit_muduo::TimeStamp(daoPj.m_ctime),
    };
}

static std::vector<kit_domain::Project> CovertDomainProjects(const std::vector<kit_dao::Project> &daoPjs)
{
    std::vector<kit_domain::Project> ans;
    for(const auto &p : daoPjs)
        ans.emplace_back(CovertDomainProject(p));
    return ans;
}


static kit_dao::Project  CovertDaoProject(const kit_domain::Project &domainPj)
{
    return kit_dao::Project {
        domainPj.m_id,
        domainPj.m_name,
        static_cast<int32_t>(domainPj.m_mode),
        static_cast<int32_t>(domainPj.m_protocolType),
        domainPj.m_listenPort,
        domainPj.m_targetIp,
        domainPj.m_userId,
        static_cast<int32_t>(domainPj.m_status),
        static_cast<int32_t>(domainPj.m_runtimeState),
        CovertPatternInfoBytes(domainPj.m_patternInfo),

    };
}

int64_t ProjectRepository::Create(kit_muduo::HttpContextPtr ctx, Project &domainPj)
{
    return _dao->Insert(ctx, CovertDaoProject(domainPj));
}

bool ProjectRepository::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectStatus status)
{
    return _dao->UpdateStatus(ctx, projectId, static_cast<int32_t>(status));
}

bool ProjectRepository::UpdateRuntimeStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectRuntimeState runtime_state, uint16_t listenPort)
{
    return _dao->UpdateRuntimeStatus(ctx, projectId, static_cast<int32_t>(runtime_state), listenPort);
}

bool ProjectRepository::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name)
{
    return  _dao->UpdateName(ctx, projectId, name);
}


Project ProjectRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId)
{
    return CovertDomainProject(_dao->GetById(ctx, projectId));
}

std::vector<Project> ProjectRepository::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, ProjectStatus status, int32_t offset, int32_t limit)
{
    return CovertDomainProjects(_dao->GetByUser(ctx, userId, static_cast<int32_t>(status), offset, limit));
}

std::vector<Project> ProjectRepository::GetAll(kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit)
{
    return CovertDomainProjects(_dao->GetAll(ctx, offset, limit));
}

std::vector<char> ProjectRepository::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) 
{
    return _dao->GetPatternInfoById(ctx, project_id);
}

bool ProjectRepository::UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::vector<char> pattern_info)
{
    return _dao->UpdatePatternInfo(ctx, project_id, pattern_info);
}

std::vector<Project> ProjectRepository::GetAllValid(kit_muduo::HttpContextPtr ctx)
{
    // 查出所有未软删的测试服务
    return CovertDomainProjects(_dao->GetAllByStatusAndRuntimeState(ctx, static_cast<int32_t>(ProjectStatus::kValid), -1));
}

std::vector<Project> ProjectRepository::GetAllActive(kit_muduo::HttpContextPtr ctx) 
{
    // 查出所有未软删且正在运行的测试服务
    return CovertDomainProjects(_dao->GetAllByStatusAndRuntimeState(ctx, 
        static_cast<int32_t>(ProjectStatus::kValid)
        ,static_cast<int32_t>(ProjectRuntimeState::kRunning)));
}

} // kit_domain
