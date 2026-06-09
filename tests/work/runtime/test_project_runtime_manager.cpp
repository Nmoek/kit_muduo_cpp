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
#include "domain/protocol_item.h"
#include "domain/runtime_result.h"
#include "runtime/runtime_controller.h"
#include "service/mock/svc_project_mock.h"
#include "service/mock/svc_protocol_mock.h"

#include <memory>
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

static Protocol MakeRuntimeHttpProtocol(int64_t protocol_id, int64_t project_id, const std::string &path)
{
    Protocol protocol;
    protocol.m_id = protocol_id;
    protocol.m_name = "runtime_http_protocol_" + std::to_string(protocol_id);
    protocol.m_type = ProtocolType::kHttp;
    protocol.m_projectId = project_id;
    protocol.m_status = ProtocolStatus::kValid;
    protocol.m_configState = ProtocolConfigState::kOn;
    protocol.m_reqBodyType = ProtocolBodyType::kJson;
    protocol.m_respBodyType = ProtocolBodyType::kJson;
    protocol.m_reqBodyDataStatus = 0;
    protocol.m_respBodyDataStatus = 1;
    protocol.m_reqCfg = RuntimeHttpReqCfg("GET", path);
    protocol.m_respCfg = RuntimeHttpRespCfg("200");
    protocol.m_respBodyData = {'o', 'k'};
    protocol.m_isEndian = false;
    protocol.m_ctime = kit_muduo::TimeStamp::Now();
    protocol.m_utime = kit_muduo::TimeStamp::Now();
    return protocol;
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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .Times(2)
        .WillRepeatedly(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetActiveByProject(_, project_id))
        .Times(1)
        .WillOnce(Return(std::vector<Protocol>{}));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kRunning, Gt(0)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*mocksvc, UpdateRuntimeState(_, project_id, ProjectRuntimeState::kStopped, 0))
        .Times(2)
        .WillRepeatedly(Return(true));

    auto start1 = runtime_manager->startProject(nullptr, project_id);
    ASSERT_TRUE(start1.ok()) << start1.status.message;
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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
    auto runtime_manager = std::make_shared<ProjectRuntimeManager>(mocksvc, mock_protocol_svc, 2);

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
