/**
 * @file svc_project.cpp
 * @brief
 * @author ljk5
 * @version 1.0
 * @date 2025-07-19 03:34:57
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "service/svc_project.h"
#include "repository/repo_project.h"
#include "domain/project.h"
#include "service/svc_log.h"
#include "base/event_loop_thread.h"
#include "net/http/http_server.h"

#include <iostream>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_domain {


ProjectService::ProjectService(std::shared_ptr<ProjectRepoInterface> repo)
    :ProjectSvcInterface(repo)
{ }

ProjectService::~ProjectService() { }

int64_t ProjectService::Add(kit_muduo::HttpContextPtr ctx, Project &domainPj)
{
    return repo_->Create(ctx, domainPj);
}

bool ProjectService::Del(kit_muduo::HttpContextPtr ctx, int64_t pjId)
{
    return false;
}


bool ProjectService::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t project_id, ProjectStatus status)
{
    return repo_->UpdateStatus(ctx, project_id, status);
}

bool ProjectService::UpdateRuntimeState(kit_muduo::HttpContextPtr ctx, int64_t project_id, ProjectRuntimeState runtime_state, uint16_t listen_port)
{
    return repo_->UpdateRuntimeState(ctx, project_id, runtime_state, listen_port);
}


bool ProjectService::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::string& name)
{
    return repo_->UpdateName(ctx, project_id, name);
}

Project ProjectService::GetById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return repo_->GetById(ctx, project_id);
}

std::vector<Project> ProjectService::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, ProjectStatus status, int32_t offset, int32_t limit)
{
    return repo_->GetByUser(ctx, userId, status, offset, limit);
}

std::vector<Project> ProjectService::GetAll(kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit)
{
    return repo_->GetAll(ctx, offset, limit);
}

nlohmann::json ProjectService::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return repo_->GetPatternInfoById(ctx, project_id);
}

bool ProjectService::UpdatePatternInfoWithProtocolWithdraw(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json& pattern_info)
{
    return repo_->UpdatePatternInfoWithProtocolWithdraw(ctx, project_id, pattern_info);

}

std::vector<Project> ProjectService::GetAllValid(kit_muduo::HttpContextPtr ctx)
{
    return repo_->GetAllValid(ctx);
}

std::vector<Project> ProjectService::GetAllActive(kit_muduo::HttpContextPtr ctx)
{
    return repo_->GetAllActive(ctx);
}


std::pair<std::vector<ProjectListItem>, int64_t> ProjectService::List(kit_muduo::HttpContextPtr ctx, const ProjectListQuery &query)
{
    return repo_->List(ctx, query);
}

}
