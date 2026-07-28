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
#include "dao/project.h"
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
        nlohmann::json::parse(daoPj.m_patternInfo),
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


static kit_dao::Project CovertDaoProject(const kit_domain::Project &domainPj)
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
        domainPj.m_patternInfo.dump(),
    };
}

static ProjectListItem CovertDomainProjectListItem(const std::pair<kit_dao::Project, std::string> & pair)
{
    return ProjectListItem{
        .p = CovertDomainProject(pair.first),
        .user_note = pair.second,
    };
}

static std::vector<ProjectListItem> CovertDomainProjectListItems(const std::vector<std::pair<kit_dao::Project, std::string>> &pairs)
{
    std::vector<ProjectListItem> items;
    for(auto &p : pairs)
    {
        items.push_back(CovertDomainProjectListItem(p));
    }
    return items;
}

int64_t ProjectRepository::Create(kit_muduo::HttpContextPtr ctx, Project &domainPj)
{
    return dao_->Insert(ctx, CovertDaoProject(domainPj));
}

bool ProjectRepository::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectStatus status)
{
    return dao_->UpdateStatus(ctx, projectId, static_cast<int32_t>(status));
}

bool ProjectRepository::UpdateRuntimeState(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectRuntimeState runtime_state, uint16_t listenPort)
{
    return dao_->UpdateRuntimeState(ctx, projectId, static_cast<int32_t>(runtime_state), listenPort);
}

bool ProjectRepository::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name)
{
    return  dao_->UpdateName(ctx, projectId, name);
}


Project ProjectRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId)
{
    return CovertDomainProject(dao_->GetById(ctx, projectId));
}

std::vector<Project> ProjectRepository::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, ProjectStatus status, int32_t offset, int32_t limit)
{
    return CovertDomainProjects(dao_->GetByUser(ctx, userId, static_cast<int32_t>(status), offset, limit));
}

std::vector<Project> ProjectRepository::GetAll(kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit)
{
    return CovertDomainProjects(dao_->GetAll(ctx, offset, limit));
}

nlohmann::json ProjectRepository::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return nlohmann::json::parse(dao_->GetPatternInfoById(ctx, project_id));
}

bool ProjectRepository::UpdatePatternInfoWithProtocolWithdraw(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json& pattern_info)
{
    return dao_->UpdatePatternInfoWithProtocolWithdraw(ctx, project_id, pattern_info);
}

std::vector<Project> ProjectRepository::GetAllValid(kit_muduo::HttpContextPtr ctx)
{
    // 查出所有未软删的测试服务
    return CovertDomainProjects(dao_->GetAllByStatusAndRuntimeState(ctx, static_cast<int32_t>(ProjectStatus::kValid), -1));
}

std::vector<Project> ProjectRepository::GetAllActive(kit_muduo::HttpContextPtr ctx)
{
    // 查出所有未软删且正在运行的测试服务
    return CovertDomainProjects(dao_->GetAllByStatusAndRuntimeState(ctx,
        static_cast<int32_t>(ProjectStatus::kValid)
        ,static_cast<int32_t>(ProjectRuntimeState::kRunning)));
}

std::pair<std::vector<ProjectListItem>, int64_t> ProjectRepository::List(kit_muduo::HttpContextPtr ctx, const ProjectListQuery &query)
{
    kit_dao::ProjectListQuery q;
    q.offset = query.offset;
    q.limit = query.limit;
    q.user_id = query.user_id;
    if(query.status.has_value())
    {
        q.status.emplace(static_cast<int32_t>(*query.status));
    }
    if(query.protocol_type.has_value())
    {
        q.protocol_type.emplace(static_cast<int32_t>(*query.protocol_type));
    }
    if(query.runtime_state.has_value())
    {
        q.runtime_state.emplace(static_cast<int32_t>(*query.runtime_state));
    }
    q.created_from = query.created_from;
    q.created_to = query.created_to;

    const auto& p = dao_->GetByListQuery(ctx, q);
    const auto items = CovertDomainProjectListItems(p.first);
    return {std::move(items), p.second}; 
}

} // kit_domain
