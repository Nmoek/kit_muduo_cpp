#pragma once

#include <gmock/gmock.h>
#include "work/runtime/runtime_controller.h"

namespace kit_domain {

class MockRuntimeController : public RuntimeControllerInterface {
public:
    MockRuntimeController() = default;
    virtual ~MockRuntimeController() = default;

    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(ProjectRuntimeResult, startProject, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(ProjectRuntimeResult, stopProject, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(ProjectRuntimeResult, delProject, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(RuntimeRecoverResult, recover, (kit_muduo::HttpContextPtr ctx), (override));
    MOCK_METHOD(ProjectRuntimeResult, editPatternInfo, (kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json &pattern_info), (override));
    MOCK_METHOD(ProtocolRuntimeResult, addProtocol, (kit_muduo::HttpContextPtr ctx, Protocol &p), (override));
    MOCK_METHOD(ProtocolRuntimeResult, enableProtocol, (kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id), (override));
    MOCK_METHOD(ProtocolRuntimeResult, disableProtocol, (kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id), (override));
    MOCK_METHOD(ProtocolRuntimeResult, delProtocol, (kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id), (override));
    MOCK_METHOD(ProtocolRuntimeResult, updateProtocolCfg, (kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, const nlohmann::json &patch), (override));
    MOCK_METHOD(ProtocolRuntimeResult, updateProtocolBody, (kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char> &body_data), (override));
    MOCK_METHOD(ProtocolRuntimeResult, reconfigProtocol, (kit_muduo::HttpContextPtr ctx, Protocol &p), (override));
    MOCK_METHOD(std::shared_ptr<ProjectServer>, findServer, (int64_t project_id), (override));
    MOCK_METHOD(void, addServer, (int64_t project_id, std::shared_ptr<ProjectServer> server), (override));
    MOCK_METHOD(void, removeServer, (int64_t project_id), (override));
};

} // namespace kit_domain
