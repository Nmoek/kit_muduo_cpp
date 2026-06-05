/**
 * @file repo_project.h
 * @brief 测试服务 repo层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 16:16:46
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_REPO_PROJECT_H__
#define __KIT_REPO_PROJECT_H__

#include "net/call_backs.h"
#include "domain/type.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kit_muduo {
namespace http {
class HttpContext;
}
}

namespace kit_dao {

class ProjectDaoInterface;

}

namespace kit_domain {

class Project;

class ProjectRepoInterface
{
public:
    ProjectRepoInterface(std::shared_ptr<kit_dao::ProjectDaoInterface> dao)
        :_dao(dao)
    {  }

    virtual ~ProjectRepoInterface() = default;
    
    virtual int64_t Create(kit_muduo::HttpContextPtr ctx, Project &pjdm) = 0;


    virtual bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectStatus status) = 0;

    virtual bool UpdateRuntimeStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectRuntimeState runtime_state, uint16_t listenPort) = 0;

    virtual bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name) = 0;

    virtual Project GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId) = 0;

    virtual std::vector<Project> GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, ProjectStatus status, int32_t offset, int32_t limit) = 0;

    virtual std::vector<Project> GetAll(kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit) = 0;

    virtual std::vector<char> GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual bool UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::vector<char> pattern_info) = 0;

    virtual std::vector<Project> GetAllValid(kit_muduo::HttpContextPtr ctx) = 0;

    virtual std::vector<Project> GetAllActive(kit_muduo::HttpContextPtr ctx) = 0;


protected:
    std::shared_ptr<kit_dao::ProjectDaoInterface> _dao;
};

class ProjectRepository : public ProjectRepoInterface
{
public:
    ProjectRepository(std::shared_ptr<kit_dao::ProjectDaoInterface> dao);

    ~ProjectRepository();

    int64_t Create(kit_muduo::HttpContextPtr ctx, Project &pjdm) override;

    bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectStatus status) override;

    bool UpdateRuntimeStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProjectRuntimeState runtime_state, uint16_t listenPort) override;

    bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name) override;

    Project GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId) override;
    
    std::vector<Project> GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, ProjectStatus status, int32_t offset, int32_t limit) override;

    std::vector<Project> GetAll(kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit) override;

    std::vector<char> GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    bool UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::vector<char> pattern_info) override;

    std::vector<Project> GetAllValid(kit_muduo::HttpContextPtr ctx) override;

    std::vector<Project> GetAllActive(kit_muduo::HttpContextPtr ctx) override;

};



} // namespace kit_domain
#endif // __KIT_REPO_PROJECT_H__
