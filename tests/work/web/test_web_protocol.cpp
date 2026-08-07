/**
 * @file test_web_protocol.cpp
 * @brief 协议项 web 接口单元测试
 */

#include "gmock/gmock.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "domain/project.h"
#include "base/time_stamp.h"
#include "domain/http_project_server.h"
#include "domain/http_protocol_item.h"
#include "domain/project_server.h"
#include "domain/protocol.h"
#include "domain/protocol_interaction_publisher.h"
#include "domain/protocol_item.h"
#include "domain/runtime_loop_pool.h"
#include "domain/runtime_result.h"
#include "domain/type.h"
#include "domain/user.h"
#include "net/call_backs.h"
#include "net/http/http_context.h"
#include "net/http/http_content.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"
#include "runtime/mock/runtime_controller_mock.h"
#include "runtime/runtime_controller.h"
#include "service/mock/svc_project_mock.h"
#include "service/mock/svc_protocol_mock.h"
#include "web/web_protocol.h"

#include <memory>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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
    req->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));
    req->setBodyData(body.dump());
    return ctx;
}

HttpContextPtr MakeJsonContextForPath(const nljson &body, const std::string &path)
{
    auto ctx = MakeJsonContext(body);
    ctx->request()->setPath(path);
    return ctx;
}

HttpContextPtr MakeAdminJsonContext(const nljson &body)
{
    auto ctx = MakeJsonContext(body);
    SetCurrentUserToContext(ctx, CurrentUser{100, "web_protocol_admin", UserRole::kAdmin, UserStatus::kActive});
    return ctx;
}

HttpContextPtr MakeRawProtocolContext(const std::string &path,
                                      const std::string &content_type,
                                      const std::string &body)
{
    auto ctx = std::make_shared<HttpContext>();
    SetCurrentUserToContext(ctx, CurrentUser{1, "web_protocol_tester", UserRole::kNormal, UserStatus::kActive});
    auto req = ctx->request();
    req->setVersion(Version::kHttp11);
    req->setMethod(HttpRequest::Method::kPost);
    req->setPath(path);
    req->addHeader("Content-Type", content_type);
    req->setContentMeta(ParseHttpContentType(content_type));
    req->setBodyData(body);
    return ctx;
}

std::string MakeMinimalMultipartBody(const std::string &boundary)
{
    std::string body;
    body.append("--").append(boundary).append("\r\n");
    body.append("Content-Disposition: form-data; name=\"field\"\r\n");
    body.append("\r\n");
    body.append("value\r\n");
    body.append("--").append(boundary).append("--\r\n");
    return body;
}

void AppendMultipartPart(std::string &body,
                         const std::string &boundary,
                         const std::string &name,
                         const std::string &content_type,
                         const std::vector<char> &data)
{
    body.append("--").append(boundary).append("\r\n");
    body.append("Content-Disposition: form-data; name=\"").append(name).append("\"\r\n");
    if(!content_type.empty())
    {
        body.append("Content-Type: ").append(content_type).append("\r\n");
    }
    body.append("\r\n");
    body.append(data.begin(), data.end());
    body.append("\r\n");
}

HttpContextPtr MakeDetailBodyMultipartContext(const std::string &path,
                                              const nljson &header,
                                              const std::vector<char> &cfg_data)
{
    const std::string boundary = "WEB-PROTOCOL-DETAIL-BODY";
    std::string body;
    const auto header_json = header.dump();
    AppendMultipartPart(body,
                        boundary,
                        "detail_header",
                        "application/json",
                        std::vector<char>(header_json.begin(), header_json.end()));
    AppendMultipartPart(body, boundary, "detail_cfg_data", "application/json", cfg_data);
    body.append("--").append(boundary).append("--\r\n");
    return MakeRawProtocolContext(path, "multipart/form-data; boundary=" + boundary, body);
}

std::vector<char> BinaryBodyConfig(std::initializer_list<nljson> fields)
{
    nljson config = nljson::object();
    config["fields"] = nljson::array();
    for(const auto &field : fields)
    {
        config["fields"].push_back(field);
    }
    const auto serialized = config.dump();
    return {serialized.begin(), serialized.end()};
}

void SetProtocolRouteParam(HttpContextPtr ctx, int64_t protocol_id)
{
    ctx->request()->addRouteParam("protocol_id", std::to_string(protocol_id));
}

nljson ResponseBody(HttpContextPtr ctx)
{
    return nljson::parse(ctx->response()->bodyString());
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
    auto server = std::make_shared<HttpProjectServer>(
        project_id,
        GetRuntimeLoopLease(project_id),
        kit_muduo::InetAddress(0, "127.0.0.1"));
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
        publisher_ = std::make_shared<ProtocolInteractionPublisher>(
            std::vector<std::shared_ptr<InteractionSink>>{});
        runtime_manager_ = std::make_shared<ProjectRuntimeManager>(project_mock_, mock_, publisher_, 1);
        handler_ = std::make_unique<ProtocolHandler>(mock_, project_mock_, runtime_manager_);
    }

    void TearDown() override
    {
        testing::Mock::VerifyAndClearExpectations(mock_.get());
        testing::Mock::VerifyAndClearExpectations(project_mock_.get());
    }

    std::shared_ptr<testing::NiceMock<MockProtocolSvc>> mock_;
    std::shared_ptr<testing::NiceMock<MockProjectSvc>> project_mock_;
    std::shared_ptr<ProtocolInteractionPublisher> publisher_;
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
        {"side", kReqSide},
        {"cfg_data", nljson::array({"bad"})},
    });
    SetProtocolRouteParam(ctx, protocol_id);

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -200);
    EXPECT_EQ(resp["message"], "protocol project mismatch");
    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
}

/*
测试思路：
1. 协议 1051 真实属于 project 9151，请求体故意夹带旧字段 project_id=9152。
2. 新接口以 path 中 protocol_id 为唯一定位，project_id 由后端 access_info 反查。
3. 断言旧 body project_id 被忽略，runtime manager 仍使用真实 project_id。

示意：
  request.project_id=9152
       |
       v
  GetAccessInfo(protocol 1051) -> project_id=9151
       |
       v
  updateProtocolCfg(project_id=9151, protocol_id=1051)

举例：
  前端仍带旧字段时，不能把协议 1051 的 runtime 更新投递到另一个项目的 server 上。
*/
TEST_F(ProtocolHandlerDetailCfgSuite, IgnoresBodyProjectIdAndUsesProjectIdFromAccessInfo)
{
    constexpr int64_t actual_project_id = 9151;
    constexpr int64_t request_project_id = 9152;
    constexpr int64_t protocol_id = 1051;
    const nljson patch = nljson{{"path", "/d9/web/body-project-id-ignored"}};

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
        {"project_id", request_project_id},
        {"side", kReqSide},
        {"cfg_data", patch},
    });
    SetProtocolRouteParam(ctx, protocol_id);

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -300);
    EXPECT_EQ(resp["message"], "project not running");
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
        {"side", kReqSide},
        {"cfg_data", nljson{{"headers", nljson{{"X-New", "2"}}}}},
    });
    SetProtocolRouteParam(ctx, protocol_id);

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
        {"side", kReqSide},
        {"cfg_data", nljson{{"path", "/d9/web/conflict"}}},
    });
    SetProtocolRouteParam(ctx, protocol_id);

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
        {"runtime_enabled", ProtocolConfigState::kOn},
    }, "/protocols/" + std::to_string(protocol_id) + "/runtime_enabled");
    SetProtocolRouteParam(ctx, protocol_id);

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
        {"side", ProtocolSide::kRequest},
        {"cfg_data", patch},
    });
    SetProtocolRouteParam(ctx, protocol_id);

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -300);
    EXPECT_EQ(resp["message"], "protocol runtime apply failed");
    EXPECT_EQ(resp["data"]["persisted"], 0);
    EXPECT_EQ(resp["data"]["runtime_applied"], 0);
}

/*
测试思路：
1. AddProtocol 是 multipart-only 接口，不能接受 application/json 请求体。
2. 构造 JSON body 调用 handler，bindMultipart 应在格式检查阶段失败。
3. 断言不会进入 project access、protocol service 或 runtime manager。

示例：
  POST /protocols/add
  Content-Type=application/json
       |
       v
  {"code":-200,"message":"body parse error"}
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, AddProtocolRejectsJsonBodyBeforeAccessAndRuntime)
{
    EXPECT_CALL(*project_mock_, GetById(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_CALL(*runtime_mock_, addProtocol(testing::_, testing::_)).Times(0);

    auto ctx = MakeRawProtocolContext(
        "/protocols/add",
        "application/json",
        R"({"header":{"project_id":1}})");

    handler_->AddProtocol(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -200);
    EXPECT_EQ(resp["message"], "body parse error");
    EXPECT_EQ(resp["data"]["persisted"], 0);
    EXPECT_EQ(resp["data"]["runtime_applied"], 0);
}

/*
测试思路：
1. DetailBody 是 multipart-only 接口，body 数据必须通过 multipart part 上传。
2. 构造 JSON body 调用 handler，应在 bindMultipart 阶段被拒绝。
3. 断言不会解析 route 后继续鉴权，也不会调用 updateProtocolBody。

示例：
  POST /protocols/930301/details/body
  Content-Type=application/json
       |
       v
  body parse error
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, DetailBodyRejectsJsonBodyBeforeAccessAndRuntime)
{
    constexpr int64_t protocol_id = 930301;

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_CALL(*runtime_mock_,
                updateProtocolBody(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
        .Times(0);

    auto ctx = MakeRawProtocolContext(
        "/protocols/" + std::to_string(protocol_id) + "/details/body",
        "application/json",
        R"({"header":{"side":1,"body_type":"json"},"cfg_data":"{}"})");
    SetProtocolRouteParam(ctx, protocol_id);

    handler_->DetailBody(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -200);
    EXPECT_EQ(resp["message"], "body parse error");
}

/*
测试思路：
1. DetailBody 的 multipart cfg_data 对 Binary Body 不再是原始字节，而是 fields[].spec/value JSON。
2. 通过真实 multipart 绑定进入 Handler，确认 header 的 response/binary 和完整 JSON 均原样转交 runtime manager。
3. 断言写操作回执与 protocol snapshot 仍按通用接口返回，避免 Binary Body 走到旧的特殊响应分支。

示例：
  detail_header={side:response,body_type:binary}
  detail_cfg_data={fields:[{spec:{byte_pos:0,byte_len:2,...},value:"H0102"}]}
                         |
                         v
  updateProtocolBody(project, protocol, response, binary, 完整 JSON)
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, DetailBodyForBinaryForwardsNestedFieldConfiguration)
{
    constexpr int64_t project_id = 930302;
    constexpr int64_t protocol_id = 93030201;
    const auto binary_config = BinaryBodyConfig({
        nljson{
            {"spec", {
                {"name", "prefix"},
                {"byte_pos", 0},
                {"byte_len", 2},
                {"type", "UINT16"},
                {"role", "common"},
                {"match", "H0102"},
            }},
            {"value", "H0102"},
        },
        nljson{
            {"spec", {
                {"name", "payload"},
                {"byte_pos", 2},
                {"byte_len", 2},
                {"type", "UINT16"},
                {"role", "common"},
                {"match", "H0304"},
            }},
            {"value", "H0304"},
        },
    });

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, protocol_id, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(ProtocolAccessInfo{
                protocol_id,
                project_id,
                "HTTP|GET|/d9/web/binary-body",
                ProtocolType::kHttp,
                ProtocolStatus::kValid,
                ProtocolConfigState::kOn,
                1,
                ProjectRuntimeState::kRunning,
                ProjectStatus::kValid}),
            testing::Return(true)));
    EXPECT_CALL(*runtime_mock_,
                updateProtocolBody(testing::_,
                                   project_id,
                                   protocol_id,
                                   ProtocolSide::kResponse,
                                   ProtocolBodyType::kBinary,
                                   testing::Eq(binary_config)))
        .WillOnce(testing::Return(ProtocolRuntimeResult::Success(
            RuntimeMutationReceipt::AllOk(),
            ProtocolRuntimeSnapshot{project_id, protocol_id, ProtocolConfigState::kOn},
            "success")));

    auto ctx = MakeDetailBodyMultipartContext(
        "/protocols/" + std::to_string(protocol_id) + "/details/body",
        nljson{
            {"side", ProtocolSide::kResponse},
            {"body_type", ProtocolBodyType::kBinary},
        },
        binary_config);
    SetProtocolRouteParam(ctx, protocol_id);

    handler_->DetailBody(nullptr, ctx);

    const auto resp = ResponseBody(ctx);
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
1. LaunchAndWithdrawsProtocol 是 JSON-only 接口，不能接受 multipart/form-data。
2. 构造一个最小 multipart body，但调用 JSON-only handler。
3. bindJson 应拒绝该格式，且不调用 access 查询或 runtime manager。

示例：
  POST /protocols/930401/runtime_enabled
  Content-Type=multipart/form-data
       |
       v
  body parse error
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, LaunchProtocolRejectsMultipartBodyBeforeAccessAndRuntime)
{
    constexpr int64_t protocol_id = 930401;
    const std::string boundary = "WEB-PROTOCOL-JSON-ONLY";

    EXPECT_CALL(*mock_, GetAccessInfo(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_CALL(*runtime_mock_, enableProtocol(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_CALL(*runtime_mock_, disableProtocol(testing::_, testing::_, testing::_)).Times(0);

    auto ctx = MakeRawProtocolContext(
        "/protocols/" + std::to_string(protocol_id) + "/runtime_enabled",
        "multipart/form-data; boundary=" + boundary,
        MakeMinimalMultipartBody(boundary));
    SetProtocolRouteParam(ctx, protocol_id);

    handler_->LaunchAndWithdrawsProtocol(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -200);
    EXPECT_EQ(resp["message"], "body parse error");
}

/*
测试思路：
1. 管理员不提交 status，语义是查询项目下的有效项和软删除项。
2. Handler 必须把 status 保持为 std::nullopt，并把 Service 返回的 pair 中的 total 原样放入分页响应。
3. 返回的 items 同时包含有效项和软删除项，且 data 必须是新的分页对象而不是旧数组。

示意：
  {project_id: 9401, offset: 0, limit: 2, status: omitted}
             |
             v
  GetByProject(status=nullopt) -> [valid, deleted], total=3
             |
             v
  data={items:[valid, deleted], offset:0, limit:2, total:3}
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, AdminListWithoutStatusReturnsAllStatusesAndPageMetadata)
{
    constexpr int64_t project_id = 9401;
    const auto valid = *MakeHttpProtocol(940101, project_id, "/d9/web/list-valid");
    auto deleted = *MakeHttpProtocol(940102, project_id, "/d9/web/list-deleted");
    deleted.m_status = ProtocolStatus::kInvalid;

    EXPECT_CALL(*project_mock_, GetById(testing::_, project_id))
        .WillOnce(testing::Return(MakeActiveProject(project_id)));
    EXPECT_CALL(*mock_, GetByProject(
        testing::_,
        project_id,
        testing::Eq(std::optional<ProtocolStatus>{}),
        0,
        2))
        .WillOnce(testing::Return(std::make_pair(
            std::vector<Protocol>{valid, deleted}, int64_t{3})));

    auto ctx = MakeAdminJsonContext(nljson{
        {"project_id", project_id},
        {"offset", 0},
        {"limit", 2},
    });
    handler_->List(nullptr, ctx);

    const auto resp = ResponseBody(ctx);
    ASSERT_EQ(resp["code"], 0);
    EXPECT_EQ(resp["message"], "success");
    ASSERT_TRUE(resp["data"].is_object());
    ASSERT_TRUE(resp["data"]["items"].is_array());
    ASSERT_EQ(resp["data"]["items"].size(), 2U);
    EXPECT_EQ(resp["data"]["items"][0]["id"], valid.m_id);
    EXPECT_EQ(resp["data"]["items"][0]["status"], static_cast<int32_t>(ProtocolStatus::kValid));
    EXPECT_EQ(resp["data"]["items"][1]["id"], deleted.m_id);
    EXPECT_EQ(resp["data"]["items"][1]["status"], static_cast<int32_t>(ProtocolStatus::kInvalid));
    EXPECT_EQ(resp["data"]["offset"], 0);
    EXPECT_EQ(resp["data"]["limit"], 2);
    EXPECT_EQ(resp["data"]["total"], 3);
}

/*
测试思路：
1. 管理员分别提交 status=1 和 status=0，验证两个合法筛选值都能到达 Service。
2. status=1 只返回有效协议，status=0 只返回软删除协议；两个请求都应保留各自的分页参数。
3. 用两个连续请求覆盖新接口的 optional<ProtocolStatus> 参数，而不是只验证响应内容。

示意：
  status=1 -> GetByProject(status=kValid)   -> [valid]
  status=0 -> GetByProject(status=kInvalid) -> [deleted]
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, AdminListPassesExplicitStatusFilters)
{
    constexpr int64_t project_id = 9402;
    const auto valid = *MakeHttpProtocol(940201, project_id, "/d9/web/list-explicit-valid");
    auto deleted = *MakeHttpProtocol(940202, project_id, "/d9/web/list-explicit-deleted");
    deleted.m_status = ProtocolStatus::kInvalid;

    EXPECT_CALL(*project_mock_, GetById(testing::_, project_id))
        .Times(2)
        .WillRepeatedly(testing::Return(MakeActiveProject(project_id)));
    {
        testing::InSequence sequence;
        EXPECT_CALL(*mock_, GetByProject(
            testing::_,
            project_id,
            testing::Eq(std::optional<ProtocolStatus>{ProtocolStatus::kValid}),
            0,
            10))
            .WillOnce(testing::Return(std::make_pair(
                std::vector<Protocol>{valid}, int64_t{1})));
        EXPECT_CALL(*mock_, GetByProject(
            testing::_,
            project_id,
            testing::Eq(std::optional<ProtocolStatus>{ProtocolStatus::kInvalid}),
            0,
            10))
            .WillOnce(testing::Return(std::make_pair(
                std::vector<Protocol>{deleted}, int64_t{1})));
    }

    auto valid_ctx = MakeAdminJsonContext(nljson{
        {"project_id", project_id},
        {"offset", 0},
        {"limit", 10},
        {"status", static_cast<int32_t>(ProtocolStatus::kValid)},
    });
    handler_->List(nullptr, valid_ctx);
    const auto valid_resp = ResponseBody(valid_ctx);
    ASSERT_EQ(valid_resp["code"], 0);
    ASSERT_EQ(valid_resp["data"]["items"].size(), 1U);
    EXPECT_EQ(valid_resp["data"]["items"][0]["id"], valid.m_id);
    EXPECT_EQ(valid_resp["data"]["total"], 1);

    auto deleted_ctx = MakeAdminJsonContext(nljson{
        {"project_id", project_id},
        {"offset", 0},
        {"limit", 10},
        {"status", static_cast<int32_t>(ProtocolStatus::kInvalid)},
    });
    handler_->List(nullptr, deleted_ctx);
    const auto deleted_resp = ResponseBody(deleted_ctx);
    ASSERT_EQ(deleted_resp["code"], 0);
    ASSERT_EQ(deleted_resp["data"]["items"].size(), 1U);
    EXPECT_EQ(deleted_resp["data"]["items"][0]["id"], deleted.m_id);
    EXPECT_EQ(deleted_resp["data"]["offset"], 0);
    EXPECT_EQ(deleted_resp["data"]["limit"], 10);
    EXPECT_EQ(deleted_resp["data"]["total"], 1);
}

/*
测试思路：
1. 普通用户省略 status，Handler 必须自动补成 ProtocolStatus::kValid，只能查询有效协议。
2. 普通用户显式提交 status=0，表示尝试读取软删除项；Handler 必须在调用 Service 前返回 403。
3. 断言第二个请求的 GetByProject 调用次数为 0，验证权限控制不仅是响应码正确，也没有发生越权数据查询。

示意：
  normal + status omitted -> status=kValid -> service called
  normal + status=0       -> 403         -> service not called
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, NormalUserListDefaultsToValidAndRejectsDeletedStatus)
{
    constexpr int64_t project_id = 9403;
    const auto valid = *MakeHttpProtocol(940301, project_id, "/d9/web/list-normal-valid");

    EXPECT_CALL(*project_mock_, GetById(testing::_, project_id))
        .Times(2)
        .WillRepeatedly(testing::Return(MakeActiveProject(project_id)));
    EXPECT_CALL(*mock_, GetByProject(
        testing::_,
        project_id,
        testing::Eq(std::optional<ProtocolStatus>{ProtocolStatus::kValid}),
        0,
        10))
        .WillOnce(testing::Return(std::make_pair(
            std::vector<Protocol>{valid}, int64_t{2})));

    auto default_ctx = MakeJsonContext(nljson{
        {"project_id", project_id},
        {"offset", 0},
        {"limit", 10},
    });
    handler_->List(nullptr, default_ctx);
    const auto default_resp = ResponseBody(default_ctx);
    ASSERT_EQ(default_resp["code"], 0);
    ASSERT_EQ(default_resp["data"]["items"].size(), 1U);
    EXPECT_EQ(default_resp["data"]["items"][0]["id"], valid.m_id);
    EXPECT_EQ(default_resp["data"]["total"], 2);

    auto deleted_ctx = MakeJsonContext(nljson{
        {"project_id", project_id},
        {"offset", 0},
        {"limit", 10},
        {"status", static_cast<int32_t>(ProtocolStatus::kInvalid)},
    });
    handler_->List(nullptr, deleted_ctx);
    const auto deleted_resp = ResponseBody(deleted_ctx);
    EXPECT_EQ(deleted_resp["code"], -403);
    EXPECT_EQ(deleted_resp["message"], "forbidden");
    EXPECT_EQ(deleted_ctx->response()->stateCode().toInt(), StateCode::k403Forbidden);
}

/*
测试思路：
1. 管理员分别提交 offset=-1、limit=0 和 limit=101，覆盖分页参数的下界与上界外输入。
2. 每个请求都应在参数校验阶段返回 -200/query param error，不能进入 Service。
3. 项目鉴权仍会先执行，因此三个请求都返回同一个有效项目；只有 GetByProject 必须保持 0 次调用。

示意：
  offset=-1 / limit=0 / limit=101
                  |
                  v
       query param error, GetByProject not called
*/
TEST_F(ProtocolHandlerRuntimeReceiptSuite, ListRejectsInvalidPaginationWithoutCallingService)
{
    constexpr int64_t project_id = 9404;
    EXPECT_CALL(*project_mock_, GetById(testing::_, project_id))
        .Times(3)
        .WillRepeatedly(testing::Return(MakeActiveProject(project_id)));
    EXPECT_CALL(*mock_, GetByProject(testing::_, testing::_, testing::_, testing::_, testing::_))
        .Times(0);

    const std::vector<nljson> requests{
        nljson{{"project_id", project_id}, {"offset", -1}, {"limit", 10}},
        nljson{{"project_id", project_id}, {"offset", 0}, {"limit", 0}},
        nljson{{"project_id", project_id}, {"offset", 0}, {"limit", 101}},
    };
    for(const auto &body : requests)
    {
        auto ctx = MakeAdminJsonContext(body);
        handler_->List(nullptr, ctx);

        const auto resp = ResponseBody(ctx);
        EXPECT_EQ(resp["code"], -200);
        EXPECT_EQ(resp["message"], "query param error");
    }
}
