/**
 * @file svc_project.h
 * @brief  测试服务 servie层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 15:10:43
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_SVC_PROJECT_H__
#define __KIT_SVC_PROJECT_H__

#include "net/call_backs.h"
#include <vector>

namespace kit_domain
{
class Project;
class ProjectRepoInterface;

class ProjectSvcInterface
{
public:
    ProjectSvcInterface(std::shared_ptr<ProjectRepoInterface> repo): _repo(repo) { }
    virtual ~ProjectSvcInterface() = default;

    virtual int64_t Add(kit_muduo::HttpContextPtr ctx, Project &domainPj) = 0;

    virtual bool Del(kit_muduo::HttpContextPtr ctx, int64_t pjId) = 0;

    virtual bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::string& name) = 0;

    virtual bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t project_id, bool status) = 0;


    virtual Project GetById(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual std::vector<Project> GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit) = 0;

    virtual std::vector<char> GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual bool UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info) = 0;

    virtual std::vector<Project> GetAllActive(kit_muduo::HttpContextPtr ctx) = 0;

protected:
    std::shared_ptr<ProjectRepoInterface> _repo;
};

class ProjectService: public ProjectSvcInterface
{
public:
    ProjectService(std::shared_ptr<ProjectRepoInterface> repo);

    ~ProjectService();

    int64_t Add(kit_muduo::HttpContextPtr ctx, Project &domainPj) override;

    bool Del(kit_muduo::HttpContextPtr ctx, int64_t pjId) override;

    bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::string& name) override;

    bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t project_id, bool status) override;
    Project GetById(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    std::vector<Project> GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit) override;

    std::vector<char> GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    bool UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info) override;

    std::vector<Project> GetAllActive(kit_muduo::HttpContextPtr ctx) override;
};


} // namespace kit_domain
#endif // __KIT_SVC_PROJECT_H__