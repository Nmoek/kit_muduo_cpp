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
#include "repository/repo_log.h"
#include "domain/project.h"
#include "base/time_stamp.h"

namespace kit_domain {
ProjectRepository::ProjectRepository(std::shared_ptr<ProjectDaoInterface> dao)
    :ProjectRepoInterface(dao)
{

}

ProjectRepository::~ProjectRepository() { }


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
        static_cast<CustomTcpPatternType>(daoPj.m_patternType),
        std::move(daoPj.m_patternInfo),
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
        static_cast<int32_t>(domainPj.m_patternType),
        domainPj.m_patternInfo,

    };
}

int64_t ProjectRepository::Create(kit_muduo::HttpContextPtr ctx, Project &domainPj)
{
    return _dao->Insert(ctx, CovertDaoProject(domainPj));
}

bool ProjectRepository::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, bool status)
{
    return _dao->UpdateStatus(ctx, projectId, status);
}

bool ProjectRepository::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name)
{
    return  _dao->UpdateName(ctx, projectId, name);
}


Project ProjectRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId)
{
    return CovertDomainProject(_dao->GetById(ctx, projectId));
}

std::vector<Project> ProjectRepository::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit)
{
    return CovertDomainProjects(_dao->GetByUser(ctx, userId, offset, limit));
}

std::vector<char> ProjectRepository::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) 
{
    return _dao->GetPatternInfoById(ctx, project_id);
}

bool ProjectRepository::UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info)
{
    return _dao->UpdatePatternInfo(ctx, project_id, pattern_type, pattern_info);
}

std::vector<Project> ProjectRepository::GetAllActive(kit_muduo::HttpContextPtr ctx) 
{
    return CovertDomainProjects(_dao->GetAllByStatus(ctx, kit_domain::ProjectStatus::ON_STATUS));
}

} // kit_domain