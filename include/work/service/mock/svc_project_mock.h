#pragma once

#include <gmock/gmock.h>
#include "work/service/svc_project.h"

namespace kit_domain {

class MockProjectSvc : public ProjectSvcInterface {
public:
    MockProjectSvc()
        : ProjectSvcInterface(nullptr) {}
    virtual ~MockProjectSvc() = default;

    MOCK_METHOD(int64_t, Add, (kit_muduo::HttpContextPtr ctx, Project &domainPj), (override));
    MOCK_METHOD(bool, Del, (kit_muduo::HttpContextPtr ctx, int64_t pjId), (override));
    MOCK_METHOD(bool, UpdateName, (kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::string& name), (override));
    MOCK_METHOD(bool, UpdateStatus, (kit_muduo::HttpContextPtr ctx, int64_t project_id, ProjectStatus status), (override));
    MOCK_METHOD(bool, UpdateRuntimeState, (kit_muduo::HttpContextPtr ctx, int64_t project_id, ProjectRuntimeState runtime_state, uint16_t listen_port), (override));
    MOCK_METHOD(Project, GetById, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(std::vector<Project>, GetByUser, (kit_muduo::HttpContextPtr ctx, int64_t userId, ProjectStatus status, int32_t offset, int32_t limit), (override));
    MOCK_METHOD(std::vector<Project>, GetAll, (kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit), (override));
    MOCK_METHOD(nlohmann::json, GetPatternInfoById, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(bool, UpdatePatternInfoWithProtocolWithdraw, (kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json& pattern_info), (override));
    MOCK_METHOD(std::vector<Project>, GetAllValid, (kit_muduo::HttpContextPtr ctx), (override));
    MOCK_METHOD(std::vector<Project>, GetAllActive, (kit_muduo::HttpContextPtr ctx), (override));
};

} // namespace kit_domain
