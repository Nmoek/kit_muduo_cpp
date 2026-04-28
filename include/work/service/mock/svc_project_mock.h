#pragma once

#include <gmock/gmock.h>
#include "work/service/svc_project.h"
#include "domain/project.h"

namespace kit_domain {

class MockProjectSvc : public ProjectSvcInterface {
public:
    MockProjectSvc()
        : ProjectSvcInterface(nullptr) {}
    virtual ~MockProjectSvc() = default;

    MOCK_METHOD(int64_t, Add, (kit_muduo::HttpContextPtr ctx, Project &domainPj), (override));
    MOCK_METHOD(bool, Del, (kit_muduo::HttpContextPtr ctx, int64_t pjId), (override));
    MOCK_METHOD(bool, UpdateName, (kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::string& name), (override));
    MOCK_METHOD(bool, UpdateStatus, (kit_muduo::HttpContextPtr ctx, int64_t project_id, bool status), (override));
    MOCK_METHOD(Project, GetById, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(std::vector<Project>, GetByUser, (kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit), (override));
    MOCK_METHOD(std::vector<char>, GetPatternInfoById, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(bool, UpdatePatternInfo, (kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info), (override));
    MOCK_METHOD(std::vector<Project>, GetAllActive, (kit_muduo::HttpContextPtr ctx), (override));
};

} // namespace kit_domain
