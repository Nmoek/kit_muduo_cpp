/**
 * @file test_project_runtime_manager.cpp
 * @brief 项目运行态管理器测试
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/time_stamp.h"
#include "domain/project.h"
#include "domain/project_server.h"
#include "domain/protocol.h"
#include "domain/http_protocol_item.h"
#include "domain/protocol_interaction_publisher.h"
#include "domain/protocol_item.h"
#include "domain/runtime_result.h"
#include "runtime/runtime_controller.h"
#include "service/mock/svc_project_mock.h"
#include "service/mock/svc_protocol_mock.h"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

using namespace kit_domain;
using namespace testing;

namespace {

static Project MakeHttpProjectForStatus(int64_t project_id)
{
    Project p;
    p.m_id = project_id;
    p.m_name = "status_http_project_" + std::to_string(project_id);
    p.m_mode = ProjectMode::ServerMode;
    p.m_protocolType = ProtocolType::kHttp;
    p.m_listenPort = 0;
    p.m_targetIp = "";
    p.m_userId = 1;
    p.m_status = ProjectStatus::kValid;
    p.m_runtimeState = ProjectRuntimeState::kStopped;
    p.m_patternInfo = nlohmann::json::object();
    p.m_ctime = kit_muduo::TimeStamp::Now();
    return p;
}

static nlohmann::json RuntimeHttpReqCfg(const std::string &method, const std::string &path)
{
    return nlohmann::json{
        {"method", method},
        {"path", path},
        {"headers", nlohmann::json::object()},
    };
}

static nlohmann::json RuntimeHttpRespCfg(const std::string &status_code)
{
    return nlohmann::json{
        {"status_code", status_code},
        {"headers", nlohmann::json{{"Content-Type", "application/json"}}},
    };
}

static std::vector<char> RuntimeBody(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

static std::vector<char> RuntimeBinaryBody(const std::string &value = "H0102")
{
    return RuntimeBody(
        R"({"fields":[{"spec":{"name":"payload","byte_pos":0,"byte_len":2,"type":"UINT16","role":"common","match":"H0102"},"value":")"
        + value
        + R"("}]})");
}

static Protocol MakeRuntimeHttpProtocol(int64_t protocol_id, int64_t project_id, const std::string &path)
{
    Protocol protocol;
    protocol.m_id = protocol_id;
    protocol.m_name = "runtime_http_protocol_" + std::to_string(protocol_id);
    protocol.m_type = ProtocolType::kHttp;
    protocol.m_projectId = project_id;
    protocol.m_runtimeKey = "HTTP|GET|" + path;
    protocol.m_status = ProtocolStatus::kValid;
    protocol.m_configState = ProtocolConfigState::kOn;
    protocol.m_reqBodyType = ProtocolBodyType::kJson;
    protocol.m_respBodyType = ProtocolBodyType::kJson;
    protocol.m_reqBodyDataStatus = 0;
    protocol.m_respBodyDataStatus = 1;
    protocol.m_reqCfg = RuntimeHttpReqCfg("GET", path);
    protocol.m_respCfg = RuntimeHttpRespCfg("200");
    protocol.m_respBodyData = {'{', '}'};
    protocol.m_isEndian = false;
    protocol.m_ctime = kit_muduo::TimeStamp::Now();
    protocol.m_utime = kit_muduo::TimeStamp::Now();
    return protocol;
}

static Protocol MakeRuntimeHttpProtocolWithState(int64_t protocol_id,
                                                 int64_t project_id,
                                                 const std::string &path,
                                                 ProtocolConfigState config_state)
{
    auto protocol = MakeRuntimeHttpProtocol(protocol_id, project_id, path);
    protocol.m_configState = config_state;
    protocol.m_runtimeKey = "HTTP|GET|" + path;
    return protocol;
}

static ProtocolAccessInfo MakeProtocolAccessInfo(const Protocol &protocol,
                                                 ProjectRuntimeState runtime_state,
                                                 ProjectStatus project_status = ProjectStatus::kValid)
{
    return ProtocolAccessInfo{
        protocol.m_id,
        protocol.m_projectId,
        protocol.m_runtimeKey,
        protocol.m_type,
        protocol.m_status,
        protocol.m_configState,
        1,
        runtime_state,
        project_status,
    };
}

static Project MakeCustomTcpProjectForPattern(int64_t project_id,
                                              ProjectRuntimeState runtime_state = ProjectRuntimeState::kStopped)
{
    Project p;
    p.m_id = project_id;
    p.m_name = "pattern_custom_tcp_project_" + std::to_string(project_id);
    p.m_mode = ProjectMode::ServerMode;
    p.m_protocolType = ProtocolType::kCustomTcp;
    p.m_listenPort = 0;
    p.m_targetIp = "";
    p.m_userId = 1;
    p.m_status = ProjectStatus::kValid;
    p.m_runtimeState = runtime_state;
    p.m_patternInfo = nlohmann::json::object();
    p.m_ctime = kit_muduo::TimeStamp::Now();
    return p;
}

static nlohmann::json MinimalCustomTcpPatternInfo()
{
    return nlohmann::json::parse(R"({
        "version": 2,
        "header_bytes": 4,
        "default_order": "big",
        "length_policy": "no_length",
        "fields": [
            {"name":"start","byte_pos":0,"byte_len":2,"type":"STR","role":"start_magic","match":"HCAFE"},
            {"name":"func","byte_pos":2,"byte_len":2,"type":"STR","role":"function_code"}
        ]
    })");
}

class FakeProjectServer final : public ProjectServer
{
public:
    explicit FakeProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease)
        : ProjectServer(
            project_id,
            std::move(lease),
            kit_muduo::InetAddress(18080, "127.0.0.1"),
            "fake-runtime-server")
        , bind_addr_(18080, "127.0.0.1")
    {
    }

    ~FakeProjectServer() override
    {
        stop();
    }

    void start() override
    {
        stopped_.store(false);
    }

    bool stop() override
    {
        bool expected = false;
        if(stopped_.compare_exchange_strong(expected, true) && lease_loop_)
        {
            lease_loop_->release();
        }
        return true;
    }

    const kit_muduo::InetAddress& getBindAddr() const override
    {
        return bind_addr_;
    }

    RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> item) override
    {
        RuntimeResult<void> result;
        if(fail_next_add_)
        {
            fail_next_add_ = false;
            result.error.set(RuntimeError::kRouteConflict);
            return result;
        }
        if(!item)
        {
            result.error.set(RuntimeError::kNullProtocolItem);
            return result;
        }
        items_[item->getId()] = std::move(item);
        return result;
    }

    RuntimeResult<void> DelProtocolItem(int64_t protocol_id) override
    {
        RuntimeResult<void> result;
        if(fail_next_del_)
        {
            fail_next_del_ = false;
            result.error.set(RuntimeError::kProtocolItemNotFound);
            return result;
        }
        if(items_.erase(protocol_id) == 0)
        {
            result.error.set(RuntimeError::kProtocolItemNotFound);
        }
        return result;
    }

    RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) override
    {
        RuntimeResult<std::shared_ptr<ProtocolItem>> result;
        auto it = items_.find(protocol_id);
        if(it == items_.end())
        {
            result.error.set(RuntimeError::kProtocolItemNotFound);
            return result;
        }
        result.val = it->second;
        return result;
    }

    RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nlohmann::json &req_cfg_json) override
    {
        RuntimeResult<void> result;
        if(fail_next_req_update_)
        {
            fail_next_req_update_ = false;
            result.error.set(RuntimeError::kRouteConflict);
            return result;
        }
        auto item_result = GetProtocolItem(protocol_id);
        if(!item_result.ok())
        {
            result.error = item_result.error;
            return result;
        }
        if(!item_result.val->setReqCfg(req_cfg_json))
        {
            result.error.set(RuntimeError::kInvalidProtocolConfig);
        }
        return result;
    }

    RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nlohmann::json &resp_cfg_json) override
    {
        RuntimeResult<void> result;
        if(fail_next_resp_update_)
        {
            fail_next_resp_update_ = false;
            result.error.set(RuntimeError::kInvalidProtocolConfig);
            return result;
        }
        auto item_result = GetProtocolItem(protocol_id);
        if(!item_result.ok())
        {
            result.error = item_result.error;
            return result;
        }
        if(!item_result.val->setRespCfg(resp_cfg_json))
        {
            result.error.set(RuntimeError::kInvalidProtocolConfig);
        }
        return result;
    }

    RuntimeResult<void> UpdateBodyProtocolItem(int64_t protocol_id,
                                               ProtocolSide side,
                                               const ProtocolBodyType body_type,
                                               const std::vector<char> &body_data) override
    {
        RuntimeResult<void> result;
        if(fail_next_body_update_)
        {
            fail_next_body_update_ = false;
            result.error.set(RuntimeError::kInvalidProtocolConfig);
            return result;
        }
        auto item_result = GetProtocolItem(protocol_id);
        if(!item_result.ok())
        {
            result.error = item_result.error;
            return result;
        }
        if(ProtocolSide::kRequest == side)
        {
            item_result.val->setReqBody(body_type, body_data);
        }
        else
        {
            item_result.val->setRespBody(body_type, body_data);
        }
        return result;
    }

    RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id,
                                                  const ProtocolBodyType body_type,
                                                  const std::vector<char> &body_data) override
    {
        return UpdateBodyProtocolItem(protocol_id, ProtocolSide::kRequest, body_type, body_data);
    }

    RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id,
                                                   const ProtocolBodyType body_type,
                                                   const std::vector<char> &body_data) override
    {
        return UpdateBodyProtocolItem(protocol_id, ProtocolSide::kResponse, body_type, body_data);
    }

    std::shared_ptr<CustomTcpPattern> GetPatternInfo() override
    {
        return nullptr;
    }

    void closeAllProtocolInteractionCaches() override
    {
        for(const auto& [protocol_id, item] : items_)
        {
            (void)protocol_id;
            if(item && item->cache())
            {
                item->cache()->close();
            }
        }
    }

    void failNextAdd()
    {
        fail_next_add_ = true;
    }

    void failNextDel()
    {
        fail_next_del_ = true;
    }

    void failNextReqUpdate()
    {
        fail_next_req_update_ = true;
    }

    void failNextBodyUpdate()
    {
        fail_next_body_update_ = true;
    }

private:
    kit_muduo::InetAddress bind_addr_;
    std::unordered_map<int64_t, std::shared_ptr<ProtocolItem>> items_;
    bool fail_next_add_{false};
    bool fail_next_del_{false};
    bool fail_next_req_update_{false};
    bool fail_next_resp_update_{false};
    bool fail_next_body_update_{false};
};

static std::shared_ptr<RuntimeLease> AcquireFakeRuntimeLease(int64_t project_id)
{
    static RuntimeLoopPool loop_pool(4, "FakeRuntimeManager");
    auto result = loop_pool.acquire(project_id);
    if(!result.ok() || !result.val)
    {
        throw std::runtime_error("fake runtime loop lease failed");
    }
    return result.val;
}

static std::shared_ptr<FakeProjectServer> MakeFakeRuntimeServer(int64_t project_id)
{
    auto server = std::make_shared<FakeProjectServer>(project_id, AcquireFakeRuntimeLease(project_id));
    server->start();
    return server;
}

static std::shared_ptr<ProjectRuntimeManager> MakeRuntimeManagerForTest(
        std::shared_ptr<ProjectSvcInterface> project_svc,
        std::shared_ptr<ProtocolSvcInterface> protocol_svc,
        size_t runtime_loop_capacity = 2)
{
    auto publisher = std::make_shared<ProtocolInteractionPublisher>(
        std::vector<std::shared_ptr<InteractionSink>>{});
    return std::make_shared<ProjectRuntimeManager>(
        std::move(project_svc),
        std::move(protocol_svc),
        std::move(publisher),
        runtime_loop_capacity);
}

} // namespace

/*
测试思路：
1. 模拟 DB 中已有一个 runtime_enabled=ON 的 HTTP protocol。
2. 调用 manager.startProject，启动时应 hydrate 该协议项到运行态 server。
3. 断言 registry 中 server 可查到该 protocol item。

示例：
  Project 9601
      |
      v
  GetActiveByProject -> [Protocol 960101]
      |
      v
  ProtocolItemFactory::Create -> AddProtocolItem -> GetProtocolItem OK
*/
TEST(ProjectRuntimeManagerSuite, StartProjectHydratesRuntimeEnabledProtocols)
{
    constexpr int64_t project_id = 9601;
    constexpr int64_t protocol_id = 960101;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, project_id))
        .WillOnce(Return(std::vector<Protocol>{MakeRuntimeHttpProtocol(protocol_id, project_id, "/runtime/hydrate")}));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kRunning, Gt(0)))
        .WillOnce(Return(true));

    auto result = runtime_manager->startProject(nullptr, project_id);
    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.snapshot.project_id, project_id);
    EXPECT_EQ(result.snapshot.runtime_state, ProjectRuntimeState::kRunning);
    EXPECT_GT(result.snapshot.listen_port, 0);

    auto server = runtime_manager->findServer(project_id);
    ASSERT_NE(server, nullptr);
    auto item_result = server->GetProtocolItem(protocol_id);
    ASSERT_TRUE(item_result.ok()) << item_result.error.toMsg();
    ASSERT_NE(item_result.val, nullptr);
    EXPECT_EQ(item_result.val->getId(), protocol_id);

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. 模拟启动时 DB 返回两个 runtime_enabled=ON 的 HTTP protocol，二者 method/path 完全相同。
2. manager hydrate 第二个协议项时应遇到 route conflict 并返回失败。
3. 断言 DB 不会被写成 running，registry 中也不会留下半成品 server。

示例：
  [pc1 GET /runtime/conflict] + [pc2 GET /runtime/conflict]
      |
      v
  Add pc1 OK -> Add pc2 route conflict
      |
      v
  startProject failed, UpdateRuntimeState(running) Times(0), findServer null
*/
TEST(ProjectRuntimeManagerSuite, StartProjectRouteConflictDoesNotPersistRunningOrRegisterServer)
{
    constexpr int64_t project_id = 9602;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, project_id))
        .WillOnce(Return(std::vector<Protocol>{
            MakeRuntimeHttpProtocol(960201, project_id, "/runtime/conflict"),
            MakeRuntimeHttpProtocol(960202, project_id, "/runtime/conflict"),
        }));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kRunning, _)).Times(0);

    auto result = runtime_manager->startProject(nullptr, project_id);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kCreateProtocolItemFailed);
    EXPECT_EQ(runtime_manager->findServer(project_id), nullptr);
}

/*
测试思路：
1. 模拟 DB 返回一个 kOn 协议，但它保存的 runtime_key 与当前 req_cfg 重新生成的 key 不一致。
2. startProject hydrate 时应先用 ProtocolConfigPipeline 对账，发现不一致后把该协议置为 kReConfig，
   并跳过挂载 runtime item。
3. 项目仍可启动成功，避免单个旧脏协议阻断整个项目恢复；同时脏协议不会进入运行态。

示例：
  DB runtime_key = HTTP|GET|/old-key
  req_cfg path   = /runtime/current-key
       |
       v
  UpdateConfigState(kReConfig), no AddProtocolItem, project running
*/
TEST(ProjectRuntimeManagerSuite, StartProjectMovesRuntimeKeyMismatchProtocolToReConfigAndSkipsHydrate)
{
    constexpr int64_t project_id = 9604;
    constexpr int64_t protocol_id = 960401;
    auto protocol = MakeRuntimeHttpProtocol(protocol_id, project_id, "/runtime/current-key");
    protocol.m_runtimeKey = "HTTP|GET|/old-key";

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, project_id))
        .WillOnce(Return(std::vector<Protocol>{protocol}));
    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, protocol_id, ProtocolConfigState::kReConfig))
            .WillOnce(Return(true));
        EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kRunning, Gt(0)))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->startProject(nullptr, project_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);

    auto server = runtime_manager->findServer(project_id);
    ASSERT_NE(server, nullptr);
    auto item_result = server->GetProtocolItem(protocol_id);
    EXPECT_FALSE(item_result.ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. 新增协议选择“保存”时，config_state=kOff，只要求 DB 持久化成功。
2. manager 仍需完整校验 req/resp 并生成 runtime_key，但不能触碰运行态 server。
3. 返回 persisted=1/runtime_applied=0，协议快照保持 kOff。

示例：
  AddProtocol(kOff, GET /runtime/add-off)
       |
       v
  ProtocolSvc::Add(runtime_key=HTTP|GET|/runtime/add-off), no runtime add
*/
TEST(ProjectRuntimeManagerSuite, AddProtocolOffPersistsOnlyAndGeneratesRuntimeKey)
{
    constexpr int64_t project_id = 9901;
    constexpr int64_t protocol_id = 990101;
    auto protocol = MakeRuntimeHttpProtocolWithState(-1, project_id, "/runtime/add-off", ProtocolConfigState::kOff);
    protocol.m_runtimeKey.clear();

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, Add(_, _))
        .WillOnce(Invoke([&](kit_muduo::HttpContextPtr, Protocol &saved) {
            EXPECT_EQ(saved.m_projectId, project_id);
            EXPECT_EQ(saved.m_configState, ProtocolConfigState::kOff);
            EXPECT_EQ(saved.m_runtimeKey, "HTTP|GET|/runtime/add-off");
            return protocol_id;
        }));

    auto result = runtime_manager->addProtocol(nullptr, protocol);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_EQ(result.snapshot.protocol_id, protocol_id);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOff);
    EXPECT_EQ(protocol.m_id, protocol_id);
    EXPECT_EQ(protocol.m_runtimeKey, "HTTP|GET|/runtime/add-off");
}

/*
测试思路：
1. 新增协议在写 DB 前必须先校验完整 req/resp body。
2. req_body_type=json 但 req_body_data 是坏 JSON 时，应直接返回 kInvalidArgument。
3. 断言 ProtocolSvc::Add 不被调用，避免非法 body 落库。

示例：
  AddProtocol(kOff, req_body_type=json, req_body="{\"bad\":")
       |
       v
  ProtocolBodyPipeline failed -> no DB Add
*/
TEST(ProjectRuntimeManagerSuite, AddProtocolRejectsInvalidRequestJsonBodyBeforePersist)
{
    constexpr int64_t project_id = 9905;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        -1, project_id, "/runtime/add-invalid-req-body", ProtocolConfigState::kOff);
    protocol.m_reqBodyData = RuntimeBody(R"({"bad":)");
    protocol.m_reqBodyDataStatus = 1;

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, Add(_, _)).Times(0);

    auto result = runtime_manager->addProtocol(nullptr, protocol);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. 新增协议选择“保存并上线”时，如果项目 DB 状态是 STOPPED，后端必须拒绝。
2. 断言拒绝发生在 ProtocolSvc::Add 前，避免把 kOn 协议保存成一个没有 runtime 的半状态。
3. 返回 persisted=0/runtime_applied=0。

示例：
  Project.runtime_state=STOPPED + AddProtocol(kOn)
       |
       v
  reject before DB insert
*/
TEST(ProjectRuntimeManagerSuite, AddProtocolOnRejectsStoppedProjectBeforePersist)
{
    constexpr int64_t project_id = 9902;
    auto protocol = MakeRuntimeHttpProtocolWithState(-1, project_id, "/runtime/add-on-stopped", ProtocolConfigState::kOn);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, Add(_, _)).Times(0);

    auto result = runtime_manager->addProtocol(nullptr, protocol);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. 新增协议选择“保存并上线”且项目处于 RUNNING 时，manager 需要先写 DB，再挂 runtime。
2. fake runtime server 成功接收 ProtocolItem 后，返回 persisted=1/runtime_applied=1。
3. runtime item 的 id 必须使用 DB 返回的新 protocol_id，而不是新增前的 -1。

示例：
  AddProtocol(kOn) -> DB id=990301 -> runtime AddProtocolItem(id=990301)
*/
TEST(ProjectRuntimeManagerSuite, AddProtocolOnPersistsAndAppliesRuntime)
{
    constexpr int64_t project_id = 9903;
    constexpr int64_t protocol_id = 990301;
    auto protocol = MakeRuntimeHttpProtocolWithState(-1, project_id, "/runtime/add-on", ProtocolConfigState::kOn);
    auto project = MakeHttpProjectForStatus(project_id);
    project.m_runtimeState = ProjectRuntimeState::kRunning;

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    runtime_manager->addServer(project_id, server);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(project));
    EXPECT_CALL(*mock_protocol_svc, Add(_, _))
        .WillOnce(Invoke([&](kit_muduo::HttpContextPtr, Protocol &saved) {
            EXPECT_EQ(saved.m_runtimeKey, "HTTP|GET|/runtime/add-on");
            return protocol_id;
        }));

    auto result = runtime_manager->addProtocol(nullptr, protocol);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOn);

    auto item_result = server->GetProtocolItem(protocol_id);
    ASSERT_TRUE(item_result.ok()) << item_result.error.toMsg();
    ASSERT_NE(item_result.val, nullptr);

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. 新增并立即上线时，DB insert 成功后 runtime AddProtocolItem 失败。
2. manager 必须调用 ProtocolSvc::Del 回滚刚插入的 DB 记录。
3. 返回 persisted=0/runtime_applied=0，表示请求目标没有在 DB/runtime 中留下结果。

示例：
  DB Add -> id=990401
       |
       v
  runtime AddProtocolItem failed -> DB Del(id=990401)
*/
TEST(ProjectRuntimeManagerSuite, AddProtocolOnRuntimeFailureRollsBackInsertedProtocol)
{
    constexpr int64_t project_id = 9904;
    constexpr int64_t protocol_id = 990401;
    auto protocol = MakeRuntimeHttpProtocolWithState(-1, project_id, "/runtime/add-rollback", ProtocolConfigState::kOn);
    auto project = MakeHttpProjectForStatus(project_id);
    project.m_runtimeState = ProjectRuntimeState::kRunning;

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    server->failNextAdd();
    runtime_manager->addServer(project_id, server);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(project));
    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, Add(_, _))
            .WillOnce(Return(protocol_id));
        EXPECT_CALL(*mock_protocol_svc, Del(_, protocol_id))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->addProtocol(nullptr, protocol);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_FALSE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kReConfig 协议是待重新配置状态，不能直接上线。
2. enableProtocol 应在 GetById、UpdateConfigState 和 runtime AddProtocolItem 前拒绝。
3. 返回 persisted=0/runtime_applied=0，提示调用方走 ReconfigProtocol。

示例：
  access.config_state=kReConfig
       |
       v
  enableProtocol -> "需要重新配置协议项"
*/
TEST(ProjectRuntimeManagerSuite, EnableProtocolRejectsReConfigBeforeDbAndRuntime)
{
    constexpr int64_t project_id = 9910;
    constexpr int64_t protocol_id = 991001;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/enable-reconfig", ProtocolConfigState::kReConfig);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    runtime_manager->addServer(project_id, server);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetById(_, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, _, _)).Times(0);

    auto result = runtime_manager->enableProtocol(nullptr, project_id, protocol_id);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOff 协议上线时，manager 要先把 DB 状态改成 kOn，再把 ProtocolItem 挂入 runtime。
2. fake server 成功后，返回 persisted=1/runtime_applied=1。
3. runtime 中应能按 protocol_id 查到刚上线的 item。

示例：
  kOff -> UpdateConfigState(kOn) -> runtime AddProtocolItem
*/
TEST(ProjectRuntimeManagerSuite, EnableProtocolOffUpdatesDbThenAddsRuntime)
{
    constexpr int64_t project_id = 9911;
    constexpr int64_t protocol_id = 991101;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/enable-ok", ProtocolConfigState::kOff);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, GetById(_, protocol_id))
            .WillOnce(Return(protocol));
        EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, protocol_id, ProtocolConfigState::kOn))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->enableProtocol(nullptr, project_id, protocol_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOn);
    EXPECT_TRUE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOff 协议上线时，如果 DB 已经切到 kOn，但 runtime 挂载失败，必须回滚 DB 到 kOff。
2. 断言 UpdateConfigState 调用顺序是 kOn -> kOff。
3. 返回 persisted=0/runtime_applied=0，runtime 中没有该 item。

示例：
  UpdateConfigState(kOn) OK
       |
       v
  runtime Add failed -> UpdateConfigState(kOff)
*/
TEST(ProjectRuntimeManagerSuite, EnableProtocolRuntimeFailureRollsBackConfigState)
{
    constexpr int64_t project_id = 9912;
    constexpr int64_t protocol_id = 991201;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/enable-rollback", ProtocolConfigState::kOff);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    server->failNextAdd();
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, GetById(_, protocol_id))
            .WillOnce(Return(protocol));
        EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, protocol_id, ProtocolConfigState::kOn))
            .WillOnce(Return(true));
        EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, protocol_id, ProtocolConfigState::kOff))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->enableProtocol(nullptr, project_id, protocol_id);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_FALSE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOff 协议重复下线按幂等成功处理。
2. manager 不应调用 UpdateConfigState，也不应触碰 runtime 删除。
3. 返回 persisted=1/runtime_applied=1，表示“下线目标状态”已经满足。

示例：
  access.config_state=kOff
       |
       v
  disableProtocol -> success, no DB/runtime mutation
*/
TEST(ProjectRuntimeManagerSuite, DisableProtocolOffIsIdempotentWithoutDbOrRuntimeMutation)
{
    constexpr int64_t project_id = 9913;
    constexpr int64_t protocol_id = 991301;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/disable-off", ProtocolConfigState::kOff);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    runtime_manager->addServer(project_id, server);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, _, _)).Times(0);

    auto result = runtime_manager->disableProtocol(nullptr, project_id, protocol_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOff);

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOn 协议下线时，manager 要先把 DB 状态改成 kOff，再从 runtime 删除 item。
2. runtime 删除成功后，返回 persisted=1/runtime_applied=1。
3. fake server 中该 item 应已不存在。

示例：
  kOn -> UpdateConfigState(kOff) -> runtime DelProtocolItem
*/
TEST(ProjectRuntimeManagerSuite, DisableProtocolOnUpdatesDbThenDeletesRuntime)
{
    constexpr int64_t project_id = 9914;
    constexpr int64_t protocol_id = 991401;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/disable-on", ProtocolConfigState::kOn);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, UpdateConfigState(_, protocol_id, ProtocolConfigState::kOff))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->disableProtocol(nullptr, project_id, protocol_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOff);
    EXPECT_FALSE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOn 协议删除时，如果项目运行中，manager 先软删 DB，再删除 runtime item。
2. runtime 删除成功后，返回 persisted=1/runtime_applied=1。
3. fake server 中该 item 应被清理。

示例：
  Del(protocol kOn) -> ProtocolSvc::Del -> runtime DelProtocolItem
*/
TEST(ProjectRuntimeManagerSuite, DelProtocolOnRunningPersistsAndDeletesRuntime)
{
    constexpr int64_t project_id = 9915;
    constexpr int64_t protocol_id = 991501;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/delete-on", ProtocolConfigState::kOn);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, Del(_, protocol_id))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->delProtocol(nullptr, project_id, protocol_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);
    EXPECT_FALSE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOn 协议删除时，如果 DB 软删成功但 runtime 删除失败，manager 要调用 ReCover 恢复 DB。
2. fake server 保持 runtime item 不变，模拟删除目标没有完成。
3. 返回 persisted=0/runtime_applied=0，避免前端误判删除成功。

示例：
  DB Del OK -> runtime Del failed -> ProtocolSvc::ReCover
*/
TEST(ProjectRuntimeManagerSuite, DelProtocolRuntimeFailureRecoversDeletedProtocol)
{
    constexpr int64_t project_id = 9916;
    constexpr int64_t protocol_id = 991601;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/delete-rollback", ProtocolConfigState::kOn);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    server->failNextDel();
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, Del(_, protocol_id))
            .WillOnce(Return(true));
        EXPECT_CALL(*mock_protocol_svc, ReCover(_, protocol_id))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->delProtocol(nullptr, project_id, protocol_id);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_TRUE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kReConfig 协议允许删除，但不应触碰 runtime。
2. manager 只调用 ProtocolSvc::Del，返回 persisted=1/runtime_applied=0。
3. 该用例固定“待重配置态本来不在 runtime 中”的删除语义。

示例：
  access.config_state=kReConfig -> DB soft delete only
*/
TEST(ProjectRuntimeManagerSuite, DelProtocolReConfigPersistsOnly)
{
    constexpr int64_t project_id = 9917;
    constexpr int64_t protocol_id = 991701;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/delete-reconfig", ProtocolConfigState::kReConfig);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, Del(_, protocol_id))
        .WillOnce(Return(true));

    auto result = runtime_manager->delProtocol(nullptr, project_id, protocol_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. TCP 格式修改后，协议状态为 kReConfig，但运行中的 server 仍可能保留旧协议项。
2. 删除必须同时清理该运行项，否则随后创建相同功能码的协议会命中旧映射冲突。

示例：
  running + kReConfig + runtime item
      -> Del(protocol) -> DB soft delete + runtime item removed
*/
TEST(ProjectRuntimeManagerSuite, DelProtocolReConfigRemovesStaleRuntimeItem)
{
    constexpr int64_t project_id = 99171;
    constexpr int64_t protocol_id = 9917101;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/delete-reconfig-running", ProtocolConfigState::kReConfig);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, Del(_, protocol_id))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->delProtocol(nullptr, project_id, protocol_id);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);
    EXPECT_FALSE(server->GetProtocolItem(protocol_id).ok());

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. 普通 DetailCfg/UpdateProtocolCfg 不能修改 kReConfig 协议。
2. manager 应在读取旧 cfg 和写 DB 前拒绝，让前端走 ReconfigProtocol 提交完整配置。
3. 返回 persisted=0/runtime_applied=0。

示例：
  access.config_state=kReConfig + patch {"path":"/new"}
       |
       v
  reject "protocol need reconfig"
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolCfgRejectsReConfigBeforeReadAndWrite)
{
    constexpr int64_t project_id = 9918;
    constexpr int64_t protocol_id = 991801;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/cfg-reconfig", ProtocolConfigState::kReConfig);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetCfgById(_, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateReqCfg(_, _, _, _)).Times(0);

    auto result = runtime_manager->updateProtocolCfg(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, nlohmann::json{{"path", "/runtime/new"}});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. kOff 协议更新 request cfg 时，只写 DB，不更新 runtime。
2. manager 需要 merge patch 成完整 cfg，并重新生成 runtime_key 传给 service。
3. 返回 persisted=1/runtime_applied=0。

示例：
  old path=/runtime/cfg-off-old + patch path=/runtime/cfg-off-new
       |
       v
  UpdateReqCfg(runtime_key=HTTP|GET|/runtime/cfg-off-new, full cfg)
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolCfgOffPersistsMergedCfgAndRuntimeKeyOnly)
{
    constexpr int64_t project_id = 9919;
    constexpr int64_t protocol_id = 991901;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/cfg-off-old", ProtocolConfigState::kOff);
    auto old_cfg_root = nlohmann::json{
        {"req_cfg", protocol.m_reqCfg},
        {"resp_cfg", protocol.m_respCfg},
    };
    auto expected_new_req_cfg = RuntimeHttpReqCfg("GET", "/runtime/cfg-off-new");

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, GetCfgById(_, protocol_id))
            .WillOnce(Return(old_cfg_root));
        EXPECT_CALL(*mock_protocol_svc,
                    UpdateReqCfg(_, protocol_id, "HTTP|GET|/runtime/cfg-off-new", Eq(expected_new_req_cfg)))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->updateProtocolCfg(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, nlohmann::json{{"path", "/runtime/cfg-off-new"}});

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOff);
}

/*
测试思路：
1. kOn 协议且项目运行中，request cfg 更新必须 DB 与 runtime 同时成功。
2. manager 先写 DB，再投递 runtime 更新；成功后返回 persisted=1/runtime_applied=1。
3. fake runtime item 的 path 应变成新 path。

示例：
  DB UpdateReqCfg(new key/full cfg) OK -> runtime UpdateReqCfgProtocolItem OK
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolCfgOnRunningPersistsAndUpdatesRuntime)
{
    constexpr int64_t project_id = 9920;
    constexpr int64_t protocol_id = 992001;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/cfg-on-old", ProtocolConfigState::kOn);
    auto old_cfg_root = nlohmann::json{
        {"req_cfg", protocol.m_reqCfg},
        {"resp_cfg", protocol.m_respCfg},
    };
    auto expected_new_req_cfg = RuntimeHttpReqCfg("GET", "/runtime/cfg-on-new");

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, GetCfgById(_, protocol_id))
            .WillOnce(Return(old_cfg_root));
        EXPECT_CALL(*mock_protocol_svc,
                    UpdateReqCfg(_, protocol_id, "HTTP|GET|/runtime/cfg-on-new", Eq(expected_new_req_cfg)))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->updateProtocolCfg(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, nlohmann::json{{"path", "/runtime/cfg-on-new"}});

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 1);

    auto item_result = server->GetProtocolItem(protocol_id);
    ASSERT_TRUE(item_result.ok());
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(item_result.val);
    ASSERT_NE(http_item, nullptr);
    EXPECT_EQ(http_item->getReqCfg().path, "/runtime/cfg-on-new");

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kOn 协议运行中更新 request cfg，DB 写成功后 runtime 更新失败。
2. manager 必须用旧 runtime_key 和旧 cfg 回滚 DB。
3. fake runtime item 保持旧 path，返回 persisted=0/runtime_applied=0。

示例：
  DB UpdateReqCfg(new) OK -> runtime failed -> DB UpdateReqCfg(old)
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolCfgRuntimeFailureRollsBackDbCfgAndKeepsRuntime)
{
    constexpr int64_t project_id = 9921;
    constexpr int64_t protocol_id = 992101;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/cfg-rollback-old", ProtocolConfigState::kOn);
    auto old_cfg_root = nlohmann::json{
        {"req_cfg", protocol.m_reqCfg},
        {"resp_cfg", protocol.m_respCfg},
    };
    auto expected_new_req_cfg = RuntimeHttpReqCfg("GET", "/runtime/cfg-rollback-new");

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    server->failNextReqUpdate();
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc, GetCfgById(_, protocol_id))
            .WillOnce(Return(old_cfg_root));
        EXPECT_CALL(*mock_protocol_svc,
                    UpdateReqCfg(_, protocol_id, "HTTP|GET|/runtime/cfg-rollback-new", Eq(expected_new_req_cfg)))
            .WillOnce(Return(true));
        EXPECT_CALL(*mock_protocol_svc,
                    UpdateReqCfg(_, protocol_id, "HTTP|GET|/runtime/cfg-rollback-old", Eq(protocol.m_reqCfg)))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->updateProtocolCfg(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, nlohmann::json{{"path", "/runtime/cfg-rollback-new"}});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);

    auto item_result = server->GetProtocolItem(protocol_id);
    ASSERT_TRUE(item_result.ok());
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(item_result.val);
    ASSERT_NE(http_item, nullptr);
    EXPECT_EQ(http_item->getReqCfg().path, "/runtime/cfg-rollback-old");

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. kReConfig 协议允许继续编辑 body，但 body 编辑不能代替完整重配。
2. manager 只写 DB，不触碰 runtime，返回 persisted=1/runtime_applied=0。
3. 快照仍然是 kReConfig，提醒前端它还不能上线。

示例：
  update body(kReConfig) -> UpdateBody only
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyReConfigPersistsOnly)
{
    constexpr int64_t project_id = 9922;
    constexpr int64_t protocol_id = 992201;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-reconfig", ProtocolConfigState::kReConfig);
    const std::vector<char> body_data{'{', '"', 'n', 'e', 'w', '"', ':', 't', 'r', 'u', 'e', '}'};

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetBodyInfoById(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateBody(_, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kJson, Eq(body_data)))
        .WillOnce(Return(true));

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kJson, body_data);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kReConfig);
}

/*
测试思路：
1. DetailBody/updateProtocolBody 是单侧 body 修改入口，也必须在写 DB 前校验 body_type 与数据。
2. body_type=json 但 body_data 是坏 JSON 时，manager 应在 ProtocolSvc::UpdateBody 前拒绝。
3. 断言不会读取旧 body，也不会写 DB 或触碰 runtime。

示例：
  updateProtocolBody(response, json, "{\"bad\":")
       |
       v
  ProtocolBodyPipeline failed -> no UpdateBody
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyRejectsInvalidJsonBeforePersist)
{
    constexpr int64_t project_id = 9926;
    constexpr int64_t protocol_id = 992601;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-invalid-json", ProtocolConfigState::kOff);
    const auto invalid_body = RuntimeBody(R"({"bad":)");

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetBodyInfoById(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateBody(_, _, _, _, _)).Times(0);

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kJson, invalid_body);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. updateProtocolBody 的 XML 分支也必须在写 DB 前走 ProtocolBodyPipeline。
2. body_type=xml 但 body_data 标签未正确闭合时，应直接返回 kInvalidArgument。
3. 断言不会读取旧 body、不会调用 UpdateBody，也不会触碰 runtime。

示例：
  updateProtocolBody(request, xml, "<root><a></root>")
       |
       v
  xml body invalid -> no UpdateBody
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyRejectsInvalidXmlBeforePersist)
{
    constexpr int64_t project_id = 9928;
    constexpr int64_t protocol_id = 992801;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-invalid-xml", ProtocolConfigState::kOff);
    const auto invalid_body = RuntimeBody("<root><a></root>");

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetBodyInfoById(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateBody(_, _, _, _, _)).Times(0);

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, ProtocolBodyType::kXml, invalid_body);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. text body 仍通过 ProtocolBodyPipeline 在 DB 前做文本校验。
2. 输入 0xC3 0x28 是典型非法 UTF-8 序列，应被公共 IsUtf8Safe 校验拒绝。
3. 断言 UpdateBody 不被调用，防止二进制数据误按 text 保存。

示例：
  updateProtocolBody(response, text, [0xC3,0x28])
       |
       v
  text body invalid -> no UpdateBody
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyRejectsInvalidTextBeforePersist)
{
    constexpr int64_t project_id = 9929;
    constexpr int64_t protocol_id = 992901;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-invalid-text", ProtocolConfigState::kOff);
    const std::vector<char> invalid_body{static_cast<char>(0xC3), static_cast<char>(0x28)};

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetBodyInfoById(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateBody(_, _, _, _, _)).Times(0);

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kText, invalid_body);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. binary body 保存的是 fields[].spec/value 配置，而不是运行态裸字节。
2. 协议处于 kOff，manager 只写 DB，不触碰 runtime。
3. 断言 UpdateBody 收到完整配置 JSON，返回 persisted=1/runtime_applied=0。

示例：
  updateProtocolBody(request, binary, {fields:[{spec:{...},value:"H0102"}]})
       |
       v
  UpdateBody called once
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyBinaryPersistsFieldConfiguration)
{
    constexpr int64_t project_id = 9930;
    constexpr int64_t protocol_id = 993001;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-binary", ProtocolConfigState::kOff);
    const auto body_data = RuntimeBinaryBody();

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetBodyInfoById(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc,
                UpdateBody(_, protocol_id, ProtocolSide::kRequest, ProtocolBodyType::kBinary, Eq(body_data)))
        .WillOnce(Return(true));

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, ProtocolBodyType::kBinary, body_data);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOff);
}

/*
测试思路：
1. Binary Body 现在要求保存 fields[].spec/value 配置，旧版裸字节不能再绕过校验。
2. manager 应在调用 ProtocolSvc::UpdateBody 前拒绝非法配置。
3. 断言数据库和 runtime 都没有被修改。

示例：
  updateProtocolBody(request, binary, [0x00,0xff,0xc3,0x28])
       |
       v
  binary body invalid -> no UpdateBody
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyBinaryRejectsLegacyRawBytesBeforePersist)
{
    constexpr int64_t project_id = 9931;
    constexpr int64_t protocol_id = 993101;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-binary-legacy", ProtocolConfigState::kOff);
    const std::vector<char> legacy_raw{
        '\0',
        static_cast<char>(0xFF),
        static_cast<char>(0xC3),
        static_cast<char>(0x28),
    };

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, GetBodyInfoById(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mock_protocol_svc, UpdateBody(_, _, _, _, _)).Times(0);

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kRequest, ProtocolBodyType::kBinary, legacy_raw);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. kOn 协议运行中更新 body，manager 要先读取旧 body 用于回滚，再写 DB，再更新 runtime。
2. runtime 更新失败时，应把 DB body 回滚为旧值。
3. 返回 persisted=0/runtime_applied=0，runtime item 的 body 保持旧值。

示例：
  GetBodyInfo(old) -> UpdateBody(new) -> runtime failed -> UpdateBody(old)
*/
TEST(ProjectRuntimeManagerSuite, UpdateProtocolBodyRuntimeFailureRollsBackDbBody)
{
    constexpr int64_t project_id = 9923;
    constexpr int64_t protocol_id = 992301;
    auto protocol = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/body-rollback", ProtocolConfigState::kOn);
    protocol.m_respBodyData = {'{', '"', 'o', 'l', 'd', '"', ':', 't', 'r', 'u', 'e', '}'};
    const std::vector<char> old_body{'{', '"', 'o', 'l', 'd', '"', ':', 't', 'r', 'u', 'e', '}'};
    const std::vector<char> new_body{'{', '"', 'n', 'e', 'w', '"', ':', 't', 'r', 'u', 'e', '}'};

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);
    auto server = MakeFakeRuntimeServer(project_id);
    auto protocol_item = std::make_shared<HttpProtocolItem>();
    protocol_item->init(protocol, HttpItemReqHeaderCfg(protocol.m_reqCfg), HttpItemRespHeaderCfg(protocol.m_respCfg));
    ASSERT_TRUE(server->AddProtocolItem(protocol_item).ok());
    server->failNextBodyUpdate();
    runtime_manager->addServer(project_id, server);

    {
        InSequence seq;
        EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(protocol, ProjectRuntimeState::kRunning)),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc,
                    GetBodyInfoById(_, protocol_id, ProtocolSide::kResponse, _, _))
            .WillOnce(DoAll(SetArgReferee<3>(ProtocolBodyType::kJson),
                            SetArgReferee<4>(old_body),
                            Return(true)));
        EXPECT_CALL(*mock_protocol_svc,
                    UpdateBody(_, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kJson, Eq(new_body)))
            .WillOnce(Return(true));
        EXPECT_CALL(*mock_protocol_svc,
                    UpdateBody(_, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kJson, Eq(old_body)))
            .WillOnce(Return(true));
    }

    auto result = runtime_manager->updateProtocolBody(
        nullptr, project_id, protocol_id, ProtocolSide::kResponse, ProtocolBodyType::kJson, new_body);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kRuntimeApplyFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);

    auto item_result = server->GetProtocolItem(protocol_id);
    ASSERT_TRUE(item_result.ok());
    auto body_view = item_result.val->getRespBodyView();
    ASSERT_NE(body_view.body_data, nullptr);
    EXPECT_EQ(*body_view.body_data, old_body);

    server->stop();
    runtime_manager->removeServer(project_id);
}

/*
测试思路：
1. ReconfigProtocol 只能处理旧状态为 kReConfig 的已有协议。
2. 完整 req/resp 校验通过后，manager 要重新生成 runtime_key，并把协议状态写回 kOff。
3. 不触碰 runtime，返回 persisted=1/runtime_applied=0。

示例：
  old kReConfig + full cfg(GET /runtime/reconfig-ok)
       |
       v
  UpdateById(runtime_key=HTTP|GET|/runtime/reconfig-ok, config_state=kOff)
*/
TEST(ProjectRuntimeManagerSuite, ReconfigProtocolUpdatesExistingProtocolToOffWithRuntimeKey)
{
    constexpr int64_t project_id = 9924;
    constexpr int64_t protocol_id = 992401;
    auto input = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/reconfig-ok", ProtocolConfigState::kReConfig);
    input.m_runtimeKey.clear();

    auto access_protocol = input;
    access_protocol.m_runtimeKey = "HTTP|GET|/runtime/old-before-reconfig";
    access_protocol.m_configState = ProtocolConfigState::kReConfig;

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(access_protocol, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, UpdateById(_, _))
        .WillOnce(Invoke([&](kit_muduo::HttpContextPtr, Protocol &saved) {
            EXPECT_EQ(saved.m_id, protocol_id);
            EXPECT_EQ(saved.m_projectId, project_id);
            EXPECT_EQ(saved.m_type, ProtocolType::kHttp);
            EXPECT_EQ(saved.m_status, ProtocolStatus::kValid);
            EXPECT_EQ(saved.m_configState, ProtocolConfigState::kOff);
            EXPECT_EQ(saved.m_runtimeKey, "HTTP|GET|/runtime/reconfig-ok");
            return true;
        }));

    auto result = runtime_manager->reconfigProtocol(nullptr, input);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_EQ(result.snapshot.protocol_id, protocol_id);
    EXPECT_EQ(result.snapshot.config_state, ProtocolConfigState::kOff);
    EXPECT_EQ(input.m_runtimeKey, "HTTP|GET|/runtime/reconfig-ok");
    EXPECT_EQ(input.m_configState, ProtocolConfigState::kOff);
}

/*
测试思路：
1. ReconfigProtocol 不能处理 kOff/kOn 协议，它不是普通编辑入口。
2. 当旧状态不是 kReConfig 时，manager 应在 UpdateById 前拒绝。
3. 返回 persisted=0/runtime_applied=0。

示例：
  access.config_state=kOff + ReconfigProtocol
       |
       v
  reject before DB update
*/
TEST(ProjectRuntimeManagerSuite, ReconfigProtocolRejectsNonReConfigProtocol)
{
    constexpr int64_t project_id = 9925;
    constexpr int64_t protocol_id = 992501;
    auto input = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/reconfig-reject", ProtocolConfigState::kOff);

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(input, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, UpdateById(_, _)).Times(0);

    auto result = runtime_manager->reconfigProtocol(nullptr, input);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. ReconfigProtocol 会重写完整协议项，因此也必须校验完整 req/resp body。
2. req body 合法但 resp_body_type=json/resp_body_data 非法时，应在 UpdateById 前拒绝。
3. 该用例固定 response body 也会被独立校验，避免只校验 request body 的漏检。

示例：
  ReconfigProtocol(req="{}", resp="{\"bad\":")
       |
       v
  response body invalid -> no UpdateById
*/
TEST(ProjectRuntimeManagerSuite, ReconfigProtocolRejectsInvalidResponseJsonBodyBeforePersist)
{
    constexpr int64_t project_id = 9927;
    constexpr int64_t protocol_id = 992701;
    auto input = MakeRuntimeHttpProtocolWithState(
        protocol_id, project_id, "/runtime/reconfig-invalid-resp-body", ProtocolConfigState::kReConfig);
    input.m_reqBodyData = RuntimeBody("{}");
    input.m_reqBodyDataStatus = 1;
    input.m_respBodyData = RuntimeBody(R"({"bad":)");
    input.m_respBodyDataStatus = 1;

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mock_protocol_svc, GetAccessInfo(_, protocol_id, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeProtocolAccessInfo(input, ProjectRuntimeState::kStopped)),
                        Return(true)));
    EXPECT_CALL(*mock_protocol_svc, UpdateById(_, _)).Times(0);

    auto result = runtime_manager->reconfigProtocol(nullptr, input);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. 对同一 project 连续调用 startProject 两次。
2. 第二次 start 应识别 registry 中已有 running server，返回幂等成功，不重复 hydrate。
3. 再连续调用 stopProject 两次，第二次 stop 在 registry 为空时仍返回成功。

示例：
  start #1 -> create/start/register/update DB
  start #2 -> registry hit, no GetActiveByProject
  stop  #1 -> stop/remove/update DB
  stop  #2 -> no runtime, update stopped again, success
*/
TEST(ProjectRuntimeManagerSuite, RepeatedStartAndStopAreIdempotent)
{
    constexpr int64_t project_id = 9603;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .Times(2)
        .WillRepeatedly(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, project_id))
        .Times(1)
        .WillOnce(Return(std::vector<Protocol>{}));
    uint16_t persisted_running_port = 0;
    {
        InSequence seq;
        EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kRunning, Gt(0)))
            .Times(1)
            .WillOnce(DoAll(SaveArg<3>(&persisted_running_port), Return(true)));
        EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kStopped,
                         Truly([&persisted_running_port](uint16_t listen_port) {
                             return listen_port > 0 && listen_port == persisted_running_port;
                         })))
            .WillOnce(Return(true));
        EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kStopped, 0))
            .WillOnce(Return(true));
    }

    auto start1 = runtime_manager->startProject(nullptr, project_id);
    ASSERT_TRUE(start1.ok()) << start1.status.message;
    EXPECT_EQ(start1.snapshot.listen_port, persisted_running_port);
    auto server = runtime_manager->findServer(project_id);
    ASSERT_NE(server, nullptr);

    auto start2 = runtime_manager->startProject(nullptr, project_id);
    ASSERT_TRUE(start2.ok()) << start2.status.message;
    EXPECT_EQ(start2.snapshot.listen_port, start1.snapshot.listen_port);
    EXPECT_EQ(runtime_manager->findServer(project_id), server);

    auto stop1 = runtime_manager->stopProject(nullptr, project_id);
    ASSERT_TRUE(stop1.ok()) << stop1.status.message;
    EXPECT_EQ(runtime_manager->findServer(project_id), nullptr);
    EXPECT_FALSE(server->isActive());

    auto stop2 = runtime_manager->stopProject(nullptr, project_id);
    ASSERT_TRUE(stop2.ok()) << stop2.status.message;
    EXPECT_EQ(stop2.snapshot.runtime_state, ProjectRuntimeState::kStopped);
    EXPECT_EQ(stop2.snapshot.listen_port, 0);
}

/*
测试思路：
1. 通过 manager.startProject 启动一个 HTTP runtime server，并确认 registry 中可查到。
2. 直接调用 manager.shutdown，模拟 Application::shutdown 的运行态收尾。
3. 断言 runtime server 已停止，registry 已清空；shutdown 不额外回写 stopped，保持重启恢复策略不变。

示例：
  startProject(9651) -> runtime_projects_[9651] = active server
      |
      v
  shutdown()
      |
      v
  server.stop() + runtime_projects_.clear() + loop_pool.shutdown()
*/
TEST(ProjectRuntimeManagerSuite, RuntimeManagerShutdownStopsServersAndClearsRegistry)
{
    constexpr int64_t project_id = 9651;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, project_id))
        .WillOnce(Return(std::vector<Protocol>{}));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kRunning, Gt(0)))
        .WillOnce(Return(true));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kStopped, 0)).Times(0);

    auto start = runtime_manager->startProject(nullptr, project_id);
    ASSERT_TRUE(start.ok()) << start.status.message;

    auto server = runtime_manager->findServer(project_id);
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->isActive());

    runtime_manager->shutdown();

    EXPECT_EQ(runtime_manager->findServer(project_id), nullptr);
    EXPECT_FALSE(server->isActive());
}

/*
测试思路：
1. 模拟数据库中有两个需要 recover 的 project：第一个 HTTP project 可正常恢复，第二个协议类型非法。
2. recover 应逐个处理：成功项注册 runtime server 并回写 running，失败项返回失败但保留自己的 project_id。
3. 最后断言批处理整体失败用于启动告警，但成功项没有被回滚，失败项也不会留下 runtime server。

示例：
  GetAllActive -> [9701(valid HTTP), 9702(invalid protocol type)]
      |
      v
  recover 9701 ok -> findServer(9701) != null
  recover 9702 failed -> runtime_projects[1].snapshot.project_id == 9702
*/
TEST(ProjectRuntimeManagerSuite, RecoverKeepsFailedProjectIdAndContinuesOtherProjects)
{
    constexpr int64_t success_project_id = 9701;
    constexpr int64_t failed_project_id = 9702;

    auto success_project = MakeHttpProjectForStatus(success_project_id);
    auto failed_project = MakeHttpProjectForStatus(failed_project_id);
    failed_project.m_protocolType = ProtocolType::kUnknown;

    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetAllActive(_))
        .WillOnce(Return(std::vector<Project>{success_project, failed_project}));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, success_project_id))
        .WillOnce(Return(std::vector<Protocol>{}));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, failed_project_id)).Times(0);
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, success_project_id, ProjectRuntimeState::kRunning, Gt(0)))
        .WillOnce(Return(true));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, failed_project_id, _, _)).Times(0);

    auto result = runtime_manager->recover(nullptr);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInternalError);
    EXPECT_EQ(result.recovered_count, 1);
    EXPECT_EQ(result.failed_count, 1);
    ASSERT_EQ(result.runtime_projects.size(), 2u);

    EXPECT_TRUE(result.runtime_projects[0].ok());
    EXPECT_EQ(result.runtime_projects[0].snapshot.project_id, success_project_id);
    EXPECT_EQ(result.runtime_projects[0].snapshot.runtime_state, ProjectRuntimeState::kRunning);
    EXPECT_GT(result.runtime_projects[0].snapshot.listen_port, 0);

    EXPECT_FALSE(result.runtime_projects[1].ok());
    EXPECT_EQ(result.runtime_projects[1].status.code, RuntimeControlCode::kProjectTypeInvalid);
    EXPECT_EQ(result.runtime_projects[1].snapshot.project_id, failed_project_id);
    EXPECT_EQ(result.runtime_projects[1].snapshot.runtime_state, ProjectRuntimeState::kStopped);
    EXPECT_EQ(result.runtime_projects[1].snapshot.listen_port, 0);

    auto success_server = runtime_manager->findServer(success_project_id);
    ASSERT_NE(success_server, nullptr);
    EXPECT_EQ(runtime_manager->findServer(failed_project_id), nullptr);

    success_server->stop();
    runtime_manager->removeServer(success_project_id);
}

/*
测试思路：
1. 模拟一个已停止的 Custom TCP project，pattern_info 是合法 schema。
2. 调用 manager.editPatternInfo，运行态层应串行化该 project 的编辑命令，并委托 service 执行
   UpdatePatternInfoWithProtocolWithdraw。
3. 断言返回 persistedOk/runtime_applied=0，表示 DB 中 pattern_info 更新和协议项撤回已生效，
   但没有运行态 server 需要热更新。

示例：
  CustomTcp Project 9801(stopped)
      |
      v
  editPatternInfo(valid schema)
      |
      v
  UpdatePatternInfoWithProtocolWithdraw -> success
*/
TEST(ProjectRuntimeManagerSuite, EditPatternInfoStoppedCustomTcpPersistsAndWithdrawsProtocols)
{
    constexpr int64_t project_id = 9801;
    const nlohmann::json pattern_info = MinimalCustomTcpPatternInfo();
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeCustomTcpProjectForPattern(project_id)));
    EXPECT_CALL(*mocksvc, UpdatePatternInfoWithProtocolWithdraw(_, project_id, Eq(pattern_info)))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, _)).Times(0);

    auto result = runtime_manager->editPatternInfo(nullptr, project_id, pattern_info);

    ASSERT_TRUE(result.ok()) << result.status.message;
    EXPECT_EQ(result.status.code, RuntimeControlCode::kOk);
    EXPECT_EQ(result.receipt.persisted, 1);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
    EXPECT_EQ(result.snapshot.project_id, project_id);
    EXPECT_EQ(result.snapshot.runtime_state, ProjectRuntimeState::kStopped);
    EXPECT_EQ(result.snapshot.listen_port, 0);
    EXPECT_EQ(runtime_manager->findServer(project_id), nullptr);
}

/*
测试思路：
1. 模拟 Custom TCP project 在 DB 中处于 running 状态。
2. 调用 editPatternInfo 时，运行态测试项未停止，manager 应在 service 持久化前拒绝。
3. 断言不会调用 UpdatePatternInfoWithProtocolWithdraw，避免运行中 schema 被换掉导致已上线协议项
   和当前 parser 语义不一致。

示例：
  CustomTcp Project 9802(running)
      |
      v
  editPatternInfo(valid schema) -> reject before DB write
*/
TEST(ProjectRuntimeManagerSuite, EditPatternInfoRejectsRunningProject)
{
    constexpr int64_t project_id = 9802;
    const nlohmann::json pattern_info = MinimalCustomTcpPatternInfo();
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeCustomTcpProjectForPattern(project_id, ProjectRuntimeState::kRunning)));
    EXPECT_CALL(*mocksvc, UpdatePatternInfoWithProtocolWithdraw(_, _, _)).Times(0);

    auto result = runtime_manager->editPatternInfo(nullptr, project_id, pattern_info);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. 模拟一个已停止的 HTTP project。
2. 即使传入合法 Custom TCP pattern_info，manager 也应拒绝非 Custom TCP 项目修改 schema。
3. 断言不会触发协议项撤回，因为 MQTT/HTTP/ONVIF 等项目不使用 Custom TCP pattern_info 作为
   协议项配置 schema。

示例：
  HTTP Project 9803(stopped)
      |
      v
  editPatternInfo(valid tcp schema) -> project type invalid
*/
TEST(ProjectRuntimeManagerSuite, EditPatternInfoRejectsNonCustomTcpProject)
{
    constexpr int64_t project_id = 9803;
    const nlohmann::json pattern_info = MinimalCustomTcpPatternInfo();
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mocksvc, UpdatePatternInfoWithProtocolWithdraw(_, _, _)).Times(0);

    auto result = runtime_manager->editPatternInfo(nullptr, project_id, pattern_info);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kProjectTypeInvalid);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. 模拟一个已停止的 Custom TCP project。
2. 传入空对象作为 pattern_info，CustomTcpPatternSpec::FromJson 应校验失败。
3. 断言 manager 在进入 DB 写入前返回 pattern info invalid，旧协议项不会被误撤回。

示例：
  CustomTcp Project 9804(stopped)
      |
      v
  editPatternInfo({}) -> schema invalid -> no DB write
*/
TEST(ProjectRuntimeManagerSuite, EditPatternInfoRejectsInvalidPatternInfo)
{
    constexpr int64_t project_id = 9804;
    const nlohmann::json invalid_pattern_info = nlohmann::json::object();
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeCustomTcpProjectForPattern(project_id)));
    EXPECT_CALL(*mocksvc, UpdatePatternInfoWithProtocolWithdraw(_, _, _)).Times(0);

    auto result = runtime_manager->editPatternInfo(nullptr, project_id, invalid_pattern_info);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kInvalidArgument);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}

/*
测试思路：
1. 模拟 Custom TCP project 已停止，pattern_info 也合法。
2. service 的 UpdatePatternInfoWithProtocolWithdraw 返回 false，代表事务写入失败。
3. 断言 manager 返回 kPersistFailed，receipt 保持全失败，避免调用方误以为协议项已经进入
   待重配置状态。

示例：
  CustomTcp Project 9805(stopped)
      |
      v
  UpdatePatternInfoWithProtocolWithdraw -> false
      |
      v
  editPatternInfo -> persist failed
*/
TEST(ProjectRuntimeManagerSuite, EditPatternInfoReturnsPersistFailedWhenServiceUpdateFails)
{
    constexpr int64_t project_id = 9805;
    const nlohmann::json pattern_info = MinimalCustomTcpPatternInfo();
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto runtime_manager = MakeRuntimeManagerForTest(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeCustomTcpProjectForPattern(project_id)));
    EXPECT_CALL(*mocksvc, UpdatePatternInfoWithProtocolWithdraw(_, project_id, Eq(pattern_info)))
        .WillOnce(Return(false));

    auto result = runtime_manager->editPatternInfo(nullptr, project_id, pattern_info);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status.code, RuntimeControlCode::kPersistFailed);
    EXPECT_EQ(result.receipt.persisted, 0);
    EXPECT_EQ(result.receipt.runtime_applied, 0);
}
