/**
 * @file test_web_protocol.cpp
 * @brief 协议项 web 接口单元测试
 */

#include "gmock/gmock.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "domain/project.h"
#include "base/time_stamp.h"
#include "domain/http_protocol_item.h"
#include "domain/project_server.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "domain/runtime_loop_pool.h"
#include "domain/runtime_result.h"
#include "domain/type.h"
#include "domain/user.h"
#include "net/call_backs.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"
#include "runtime/mock/runtime_controller_mock.h"
#include "runtime/runtime_controller.h"
#include "service/mock/svc_project_mock.h"
#include "service/mock/svc_protocol_mock.h"
#include "web/web_protocol.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo;
using namespace kit_muduo::http;

namespace {

constexpr ProtocolSide kReqSide = ProtocolSide::kRequest;

nljson HttpReqCfg(const std::string &method,
                  const std::string &path,
                  const nljson &headers)
{
    return nljson{
        {"method", method},
        {"path", path},
        {"headers", headers},
    };
}

nljson HttpRespCfg(const std::string &status_code,
                   const nljson &headers)
{
    return nljson{
        {"status_code", status_code},
        {"headers", headers},
    };
}

std::shared_ptr<Protocol> MakeHttpProtocol(
        int64_t protocol_id,
        int64_t project_id,
        const std::string &path)
{
    auto protocol = std::make_shared<Protocol>();
    protocol->m_id = protocol_id;
    protocol->m_name = "web_protocol_pc_" + std::to_string(protocol_id);
    protocol->m_type = ProtocolType::kHttp;
    protocol->m_projectId = project_id;
    protocol->m_status = ProtocolStatus::kValid;
    protocol->m_reqBodyType = ProtocolBodyType::kJson;
    protocol->m_respBodyType = ProtocolBodyType::kJson;
    protocol->m_reqBodyDataStatus = 0;
    protocol->m_respBodyDataStatus = 1;
    protocol->m_reqCfg = HttpReqCfg("GET", path, nljson{{"X-Old", "1"}});
    protocol->m_respCfg = HttpRespCfg("200", nljson{{"Content-Type", "application/json"}});
    protocol->m_reqBodyData = {};
    protocol->m_respBodyData = {'o', 'k'};
    protocol->m_isEndian = true;
    protocol->m_ctime = TimeStamp::Now();
    protocol->m_utime = TimeStamp::Now();
    return protocol;
}

Project MakeActiveProject(int64_t project_id)
{
    Project project;
    project.m_id = project_id;
    project.m_name = "web_protocol_project_" + std::to_string(project_id);
    project.m_status = ProjectStatus::kValid;
    project.m_userId = 1;
    return project;
}

HttpContextPtr MakeJsonContext(const nljson &body)
{
    auto ctx = std::make_shared<HttpContext>();
    SetCurrentUserToContext(ctx, CurrentUser{1, "web_protocol_tester", UserRole::kNormal, UserStatus::kActive});
    auto req = ctx->request();
    req->setVersion(Version::kHttp11);
    req->setMethod(HttpRequest::Method::kPost);
    req->setPath("/protocols/details/cfg");
    req->addHeader("Content-Type", "application/json");

    Body req_body((ContentType(ContentType::kJsonType)));
    
    req_body.appendData(body.dump());
    req->setBody(req_body);
    return ctx;
}

HttpContextPtr MakeJsonContextForPath(const nljson &body, const std::string &path)
{
    auto ctx = MakeJsonContext(body);
    ctx->request()->setPath(path);
    return ctx;
}

nljson ResponseBody(HttpContextPtr ctx)
{
    return nljson::parse(ctx->response()->body().toString());
}

std::shared_ptr<HttpProtocolItem> GetHttpRuntimeItem(
        const std::shared_ptr<HttpProjectServer> &server,
        int64_t protocol_id)
{
    auto result = server->GetProtocolItem(protocol_id);
    if(!result.ok())
    {
        return nullptr;
    }
    return std::dynamic_pointer_cast<HttpProtocolItem>(result.val);
}

std::shared_ptr<RuntimeLease> GetRuntimeLoopLease(int64_t project_id)
{
    static RuntimeLoopPool loop_pool(2);
    auto result = loop_pool.acquire(project_id);
    if(!result.ok() || !result.val)
    {
        throw std::runtime_error("runtime loop lease faild");
    }
    return result.val;
}

std::shared_ptr<HttpProjectServer> MakeHttpRuntimeServer(
        int64_t project_id,
        const std::vector<std::shared_ptr<Protocol>> &protocols)
{
    auto server = std::make_shared<HttpProjectServer>(project_id, GetRuntimeLoopLease(project_id));
    for(const auto &protocol : protocols)
    {
        auto item = ProtocolItemFactory::Create(protocol, server);
        EXPECT_NE(item, nullptr);
        if(!item)
        {
            continue;
        }
        auto add_result = server->AddProtocolItem(item);
        EXPECT_TRUE(add_result.ok()) << add_result.error.toMsg();
    }
    return server;
}

testing::Matcher<const nljson&> JsonEq(const nljson &expected)
{
    return testing::Truly([expected](const nljson &actual) {
        return actual == expected;
    });
}

class ProtocolHandlerDetailCfgSuite : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mock_ = std::make_shared<testing::NiceMock<MockProtocolSvc>>();
        project_mock_ = std::make_shared<testing::NiceMock<MockProjectSvc>>();
        runtime_manager_ = std::make_shared<ProjectRuntimeManager>(project_mock_, mock_, 1);
        handler_ = std::make_unique<ProtocolHandler>(mock_, project_mock_, runtime_manager_);
    }

    void TearDown() override
    {
        testing::Mock::VerifyAndClearExpectations(mock_.get());
        testing::Mock::VerifyAndClearExpectations(project_mock_.get());
    }

    std::shared_ptr<testing::NiceMock<MockProtocolSvc>> mock_;
    std::shared_ptr<testing::NiceMock<MockProjectSvc>> project_mock_;
    std::shared_ptr<ProjectRuntimeManager> runtime_manager_;
    std::unique_ptr<ProtocolHandler> handler_;
};

class ProtocolHandlerRuntimeReceiptSuite : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mock_ = std::make_shared<testing::NiceMock<MockProtocolSvc>>();
        project_mock_ = std::make_shared<testing::NiceMock<MockProjectSvc>>();
        runtime_mock_ = std::make_shared<testing::NiceMock<MockRuntimeController>>();
        handler_ = std::make_unique<ProtocolHandler>(mock_, project_mock_, runtime_mock_);
    }

    void TearDown() override
    {
        testing::Mock::VerifyAndClearExpectations(mock_.get());
        testing::Mock::VerifyAndClearExpectations(project_mock_.get());
        testing::Mock::VerifyAndClearExpectations(runtime_mock_.get());
    }

    std::shared_ptr<testing::NiceMock<MockProtocolSvc>> mock_;
    std::shared_ptr<testing::NiceMock<MockProjectSvc>> project_mock_;
    std::shared_ptr<testing::NiceMock<MockRuntimeController>> runtime_mock_;
    std::unique_ptr<ProtocolHandler> handler_;
};

} // namespace

/*
测试思路：
1. 构造 DetailCfg 请求，但 cfg_data 传数组而不是 object。
2. 直接调用 ProtocolHandler::DetailCfg。
3. 04 后 handler 只做鉴权并委托 manager，manager 再校验 cfg_data；断言失败后不会读取旧 cfg、写 DB 或触碰 runtime。

示意：
  HTTP body cfg_data = ["bad"]
           |
           v
  CheckProtocolAccess OK -> manager is_object() == false -> return -200

举例：
  前端误传 cfg_data: [] 时，可以发生 handler/manager 两次 access 查询，但不应继续读写协议配置，也不应触碰运行态 server。
*/
TEST_F(ProtocolHandlerDetailCfgSuite, RejectsNonObjectCfgDataBeforeServiceAndRuntime)
{
    constexpr int64_t project_id = 9101;
    constexpr int64_t protocol_id = 1001;

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
        .Times(2)
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(ProtocolAccessInfo{
                protocol_id,
                project_id,
                "HTTP|GET|/d9/web/invalid-cfg",
                ProtocolType::kHttp,
                ProtocolStatus::kValid,
                ProtocolConfigState::kOn,
                1,
                ProjectRuntimeState::kRunning,
                ProjectStatus::kValid}
            )
            ,testing::Return(true)
        ));
    EXPECT_CALL(*mock_, GetCfgById(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*mock_, UpdateReqCfg(testing::_, testing::_, testing::_, testing::_)).Times(0);
    EXPECT_CALL(*mock_, UpdateRespCfg(testing::_, testing::_, testing::_)).Times(0);

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", project_id},
        {"type", ProtocolType::kHttp},
        {"side", kReqSide},
        {"cfg_data", nljson::array({"bad"})},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -200);
    EXPECT_EQ(resp["message"], "protocol project mismatch");
    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
}

/*
测试思路：
1. 协议 1051 真实属于 project 9151，但请求体故意传 project_id=9152。
2. 04 后 handler 只做鉴权，归属一致性由 manager 再次读取 access_info 后校验。
3. 断言不会查询旧 cfg、不会写 DB，也不会根据错误 project_id 触碰 runtime server。

示意：
  request.project_id=9152
       |
       v
  GetById(protocol 1051) -> m_projectId=9151
       |
       v
  mismatch -> return -200

举例：
  前端或恶意调用方传错 project_id 时，不能把协议 1051 的 runtime 更新投递到另一个项目的 server 上。
*/
TEST_F(ProtocolHandlerDetailCfgSuite, RejectsProjectIdMismatchBeforeDbAndRuntime)
{
    constexpr int64_t actual_project_id = 9151;
    constexpr int64_t request_project_id = 9152;
    constexpr int64_t protocol_id = 1051;

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
    .Times(2)
    .WillRepeatedly(
        testing::DoAll(
            testing::SetArgReferee<2>(ProtocolAccessInfo{
                protocol_id,
                actual_project_id,
                "HTTP|GET|/d9/web/project-mismatch",
                ProtocolType::kHttp,
                ProtocolStatus::kValid,
                ProtocolConfigState::kOn,
                1,
                ProjectRuntimeState::kRunning,
                ProjectStatus::kValid}
            )
            ,testing::Return(true)
        )
    );
    EXPECT_CALL(*mock_, GetCfgById(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*mock_, UpdateReqCfg(testing::_, testing::_, testing::_, testing::_)).Times(0);
    EXPECT_CALL(*mock_, UpdateRespCfg(testing::_, testing::_, testing::_)).Times(0);

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", request_project_id},
        {"type", ProtocolType::kHttp},
        {"side", kReqSide},
        {"cfg_data", nljson{{"path", "/d9/web/should-not-apply"}}},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -200);
    EXPECT_EQ(resp["message"], "protocol project mismatch");
    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
}

/*
测试思路：
1. runtime 中已有协议 1101，DB 中旧 req cfg 为 GET /d9/web/success。
2. DetailCfg 只传局部 headers patch，例如 {"headers":{"X-New":"2"}}。
3. mock service 断言写 DB 的不是局部 patch，而是 merge_patch 后的完整 req cfg。
4. handler 返回成功后，再读取 runtime item，确认运行态也拿到了同一份完整 cfg。

示意：
  old cfg: {method,path,headers:{X-Old}}
        +  patch: {headers:{X-New}}
        =  new cfg: {method,path,headers:{X-Old,X-New}}
                    |
                    v
              DB full-string update -> runtime update

举例：
  headers 局部追加后，method/path 不会丢失，运行态 route 仍可解析完整 HTTP 请求配置。
*/
TEST_F(ProtocolHandlerDetailCfgSuite, MergesFullCfgWritesDbAndUpdatesRuntime)
{
    constexpr int64_t project_id = 9102;
    constexpr int64_t protocol_id = 1101;

    auto old_req_cfg = HttpReqCfg("GET", "/d9/web/success", nljson{{"X-Old", "1"}});
    auto old_resp_cfg = HttpRespCfg("200", nljson{{"Content-Type", "application/json"}});
    auto new_req_cfg = HttpReqCfg("GET", "/d9/web/success", nljson{{"X-Old", "1"}, {"X-New", "2"}});

    auto server = MakeHttpRuntimeServer(
        project_id,
        {MakeHttpProtocol(protocol_id, project_id, "/d9/web/success")});
    runtime_manager_->addServer(project_id, server);

    EXPECT_CALL(*mock_, UpdateRespCfg(testing::_, testing::_, testing::_)).Times(0);
    {
        testing::InSequence seq;
        EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
            .Times(2)
            .WillRepeatedly(testing::DoAll(
                testing::SetArgReferee<2>(ProtocolAccessInfo{
                    protocol_id,
                    project_id,
                    "HTTP|GET|/d9/web/success",
                    ProtocolType::kHttp,
                    ProtocolStatus::kValid,
                    ProtocolConfigState::kOn,
                    1,
                    ProjectRuntimeState::kRunning,
                    ProjectStatus::kValid}
                )
                ,testing::Return(true)
            ));
        EXPECT_CALL(*mock_, GetCfgById(testing::_, protocol_id))
            .WillOnce(testing::Return(nljson{
                {"req_cfg", old_req_cfg},
                {"resp_cfg", old_resp_cfg},
            }));
        EXPECT_CALL(*mock_, UpdateReqCfg(testing::_, protocol_id, "HTTP|GET|/d9/web/success", JsonEq(new_req_cfg)))
            .WillOnce(testing::Return(true));
    }

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", project_id},
        {"type", ProtocolType::kHttp},
        {"side", kReqSide},
        {"cfg_data", nljson{{"headers", nljson{{"X-New", "2"}}}}},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], 0);
    EXPECT_EQ(resp["message"], "success");

    auto runtime_item = GetHttpRuntimeItem(server, protocol_id);
    ASSERT_NE(runtime_item, nullptr);
    EXPECT_EQ(runtime_item->getReqCfg().path, "/d9/web/success");
    EXPECT_EQ(runtime_item->getReqCfg().headers.at("X-Old"), "1");
    EXPECT_EQ(runtime_item->getReqCfg().headers.at("X-New"), "2");
}

/*
测试思路：
1. runtime 中已有两个 HTTP 协议：
   - 协议 1201: GET /d9/web/old
   - 协议 1202: GET /d9/web/conflict
2. DetailCfg 尝试把协议 1201 的 path 更新成 /d9/web/conflict。
3. mock service 断言 DB 先写入新完整 cfg；runtime route 冲突后，handler 再用旧完整 cfg 回滚 DB。
4. 最后断言 handler 返回失败，runtime 中协议 1201 仍保持旧 path。

示意：
  DB update(new cfg) ok
          |
          v
  runtime route conflict
          |
          v
  DB rollback(old cfg)

举例：
  两个 GET exact path 冲突时，Web 接口不能留下“DB 已更新、运行态未更新”的半状态。
*/
TEST_F(ProtocolHandlerDetailCfgSuite, RuntimeFailureRollsBackDbAndKeepsRuntimeCfg)
{
    constexpr int64_t project_id = 9103;
    constexpr int64_t protocol_id = 1201;
    constexpr int64_t conflict_protocol_id = 1202;

    auto old_req_cfg = HttpReqCfg("GET", "/d9/web/old", nljson{{"X-Old", "1"}});
    auto old_resp_cfg = HttpRespCfg("200", nljson{{"Content-Type", "application/json"}});
    auto new_req_cfg = HttpReqCfg("GET", "/d9/web/conflict", nljson{{"X-Old", "1"}});

    auto server = MakeHttpRuntimeServer(
        project_id,
        {
            MakeHttpProtocol(protocol_id, project_id, "/d9/web/old"),
            MakeHttpProtocol(conflict_protocol_id, project_id, "/d9/web/conflict"),
        });
    runtime_manager_->addServer(project_id, server);

    EXPECT_CALL(*mock_, UpdateRespCfg(testing::_, testing::_, testing::_)).Times(0);
    {
        testing::InSequence seq;

        EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
        .Times(2)
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(ProtocolAccessInfo{
                protocol_id,
                project_id,
                "HTTP|GET|/d9/web/old",
                ProtocolType::kHttp,
                ProtocolStatus::kValid,
                ProtocolConfigState::kOn,
                1,
                ProjectRuntimeState::kRunning,
                ProjectStatus::kValid}
            )
            ,testing::Return(true)
        ));
        EXPECT_CALL(*mock_, GetCfgById(testing::_, protocol_id))
            .WillOnce(testing::Return(nljson{
                {"req_cfg", old_req_cfg},
                {"resp_cfg", old_resp_cfg},
            }));
        EXPECT_CALL(*mock_, UpdateReqCfg(testing::_, protocol_id, "HTTP|GET|/d9/web/conflict", JsonEq(new_req_cfg)))
            .WillOnce(testing::Return(true));
        EXPECT_CALL(*mock_, UpdateReqCfg(testing::_, protocol_id, "HTTP|GET|/d9/web/old", JsonEq(old_req_cfg)))
            .WillOnce(testing::Return(true));
    }

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", project_id},
        {"type", ProtocolType::kHttp},
        {"side", kReqSide},
        {"cfg_data", nljson{{"path", "/d9/web/conflict"}}},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -300);
    EXPECT_EQ(resp["message"], "protocol runtime apply failed");

    auto runtime_item = GetHttpRuntimeItem(server, protocol_id);
    ASSERT_NE(runtime_item, nullptr);
    EXPECT_EQ(runtime_item->getReqCfg().path, "/d9/web/old");
    EXPECT_EQ(runtime_item->getReqCfg().headers.at("X-Old"), "1");
}

/*
测试思路：
1. LaunchAndWithdrawsProtocol 现在只负责鉴权、调用 runtime manager、转换回执。
2. mock runtime manager 返回 enableProtocol 成功，并携带 persisted/runtime_applied 和 snapshot。
3. 断言 HTTP JSON 同时透出写操作回执和 protocol snapshot 字段。

示意：
  manager.enableProtocol -> persisted=1,runtime_applied=1,config_state=kOn
           |
           v
  response.data 同步包含 persisted/runtime_applied/project_id/protocol_id/config_state

举例：
  前端点击上线成功后，可以直接根据 data.config_state=1 和 runtime_applied=1 刷新按钮状态。
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, LaunchProtocolSuccessWritesRuntimeReceiptAndSnapshot)
{
    constexpr int64_t project_id = 9301;
    constexpr int64_t protocol_id = 930101;

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(ProtocolAccessInfo{
                protocol_id,
                project_id,
                "HTTP|GET|/d9/web/launch-ok",
                ProtocolType::kHttp,
                ProtocolStatus::kValid,
                ProtocolConfigState::kOff,
                1,
                ProjectRuntimeState::kRunning,
                ProjectStatus::kValid}
            ),
            testing::Return(true)));
    EXPECT_CALL(*runtime_mock_, enableProtocol(testing::_, project_id, protocol_id))
        .WillOnce(testing::Return(ProtocolRuntimeResult::Success(
            RuntimeMutationReceipt::AllOk(),
            ProtocolRuntimeSnapshot{project_id, protocol_id, ProtocolConfigState::kOn},
            "success")));

    auto ctx = MakeJsonContextForPath(nljson{
        {"id", protocol_id},
        {"runtime_enabled", ProtocolConfigState::kOn},
    }, "/protocols/launch");

    handler_->LaunchAndWithdrawsProtocol(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], 0);
    EXPECT_EQ(resp["message"], "success");
    EXPECT_EQ(resp["data"]["persisted"], 1);
    EXPECT_EQ(resp["data"]["runtime_applied"], 1);
    EXPECT_EQ(resp["data"]["project_id"], project_id);
    EXPECT_EQ(resp["data"]["protocol_id"], protocol_id);
    EXPECT_EQ(resp["data"]["config_state"], static_cast<int32_t>(ProtocolConfigState::kOn));
}

/*
测试思路：
1. DetailCfg 调用 runtime manager 更新运行中协议配置时，可能出现 DB 已写、runtime 失败、DB 已回滚的结果。
2. handler 不应自己重新解释失败，而应把 manager 的 receipt 原样转换成 HTTP JSON。
3. 断言失败响应 code/message 和 persisted/runtime_applied 与 ProtocolRuntimeResult 保持一致。

示意：
  manager.updateProtocolCfg -> failed(kRuntimeApplyFailed, receipt={0,0})
           |
           v
  response code=-300, data.persisted=0, data.runtime_applied=0

举例：
  前端保存配置遇到 runtime 冲突后，可以看到没有 DB/runtime 半成功状态。
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, DetailCfgRuntimeFailureWritesReceiptFromManager)
{
    constexpr int64_t project_id = 9302;
    constexpr int64_t protocol_id = 930201;
    const nljson patch = nljson{{"path", "/d9/web/runtime-failed"}};

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(ProtocolAccessInfo{
                protocol_id,
                project_id,
                "HTTP|GET|/d9/web/old",
                ProtocolType::kHttp,
                ProtocolStatus::kValid,
                ProtocolConfigState::kOn,
                1,
                ProjectRuntimeState::kRunning,
                ProjectStatus::kValid}
            ),
            testing::Return(true)));
    EXPECT_CALL(*runtime_mock_,
                updateProtocolCfg(testing::_, project_id, protocol_id, ProtocolSide::kRequest, JsonEq(patch)))
        .WillOnce(testing::Return(ProtocolRuntimeResult::Failed(
            RuntimeControlCode::kRuntimeApplyFailed,
            RuntimeError(RuntimeError::kRouteConflict),
            "protocol runtime apply failed",
            RuntimeMutationReceipt::AllErr(),
            ProtocolRuntimeSnapshot{project_id, protocol_id, ProtocolConfigState::kOn})));

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", project_id},
        {"type", ProtocolType::kHttp},
        {"side", ProtocolSide::kRequest},
        {"cfg_data", patch},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -300);
    EXPECT_EQ(resp["message"], "protocol runtime apply failed");
    EXPECT_EQ(resp["data"]["persisted"], 0);
    EXPECT_EQ(resp["data"]["runtime_applied"], 0);
}
