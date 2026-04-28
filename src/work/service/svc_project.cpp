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
    return _repo->Create(ctx, domainPj);
}

bool ProjectService::Del(kit_muduo::HttpContextPtr ctx, int64_t pjId)  
{ 
    return false; 
}


bool ProjectService::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, bool status)
{
    return _repo->UpdateStatus(ctx, projectId, status);
}


bool ProjectService::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name)
{
    return _repo->UpdateName(ctx, projectId, name);
}

Project ProjectService::GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId)  
{ 
    return _repo->GetById(ctx, projectId); 
}

std::vector<Project> ProjectService::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit)  
{ 
    return _repo->GetByUser(ctx, userId, offset, limit); 
}

std::vector<char> ProjectService::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return _repo->GetPatternInfoById(ctx, project_id); 
}

bool ProjectService::UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info)
{
    return _repo->UpdatePatternInfo(ctx, project_id, pattern_type, pattern_info); 

}

std::vector<Project> ProjectService::GetAllActive(kit_muduo::HttpContextPtr ctx)
{
    return _repo->GetAllActive(ctx);
}

}