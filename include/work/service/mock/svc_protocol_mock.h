#pragma once

#include <gmock/gmock.h>
#include "work/domain/protocol.h"
#include "work/service/svc_protocol.h"

namespace kit_domain {

class MockProtocolSvc : public ProtocolSvcInterface {
public:
    MockProtocolSvc()
        : ProtocolSvcInterface(nullptr) {}
    virtual ~MockProtocolSvc() = default;

    MOCK_METHOD(int64_t, Add, (kit_muduo::HttpContextPtr ctx, Protocol &domainPc), (override));
    MOCK_METHOD(bool, Del, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id), (override));
    MOCK_METHOD(bool, ReCover, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id), (override));
    MOCK_METHOD(bool, UpdateById, (kit_muduo::HttpContextPtr ctx, Protocol &domainPc), (override));
    MOCK_METHOD(bool, UpdateName, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& name), (override));
    MOCK_METHOD(bool, UpdateReqCfg, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json), (override));
    MOCK_METHOD(bool, UpdateRespCfg, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json), (override));
    MOCK_METHOD(bool, UpdateBody, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char>& cfg_data), (override));
    MOCK_METHOD(Protocol, GetById, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id), (override));
    MOCK_METHOD(std::vector<Protocol>, GetByProject, (kit_muduo::HttpContextPtr ctx, int64_t project_id, ProtocolStatus status, int32_t offset, int32_t limit), (override));
    MOCK_METHOD(std::vector<Protocol>, GetValidByProject, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(std::vector<Protocol>, GetActiveByProject, (kit_muduo::HttpContextPtr ctx, int64_t project_id), (override));
    MOCK_METHOD(int32_t, GetProtocolCnt, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolStatus status), (override));
    MOCK_METHOD(nlohmann::json, GetTcpCommonFieldsById, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side), (override));
    MOCK_METHOD(ProtocolBodyType, GetBodyTypeById, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side), (override));
    MOCK_METHOD(bool, GetBodyDataById, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, std::vector<char> &body_data), (override));
    MOCK_METHOD(bool, GetBodyInfoById, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType &body_type, std::vector<char> &body_data), (override));
    MOCK_METHOD(nlohmann::json, GetCfgById, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id), (override));
    MOCK_METHOD(bool, GetAccessInfo, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolAccessInfo& access_info), (override));
    MOCK_METHOD(bool, UpdateConfigState, (kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolConfigState config_state), (override));
};

} // namespace kit_domain
