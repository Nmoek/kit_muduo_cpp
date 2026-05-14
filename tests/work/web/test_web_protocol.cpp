/**
 * @file test_web_protocol.cpp
 * @brief 协议项 web 接口单元测试
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "application.h"
#include "base/time_stamp.h"
#include "domain/http_protocol_item.h"
#include "domain/project_server.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "domain/runtime_result.h"
#include "net/call_backs.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"
#include "service/mock/svc_protocol_mock.h"
#include "web/web_protocol.h"

#include <memory>
#include <string>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo;
using namespace kit_muduo::http;

namespace {

constexpr int32_t kReqCfg = 1;
constexpr int32_t kRespCfg = 2;

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
    protocol->m_type = ProtocolType::HTTP_PROTOCOL;
    protocol->m_projectId = project_id;
    protocol->m_status = ProtocolStatus::ACTIVE;
    protocol->m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE;
    protocol->m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE;
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

HttpContextPtr MakeJsonContext(const nljson &body)
{
    auto ctx = std::make_shared<HttpContext>();
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

std::shared_ptr<HttpProjectServer> MakeHttpRuntimeServer(
        int64_t project_id,
        const std::vector<std::shared_ptr<Protocol>> &protocols)
{
    auto server = std::make_shared<HttpProjectServer>(project_id);
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

testing::Matcher<const std::string&> JsonStringEq(const nljson &expected)
{
    return testing::Truly([expected](const std::string &actual) {
        try
        {
            return nljson::parse(actual) == expected;
        }
        catch(const std::exception&)
        {
            return false;
        }
    });
}

std::shared_ptr<testing::NiceMock<MockProtocolSvc>> SharedMockProtocolSvc()
{
    static auto mock = std::make_shared<testing::NiceMock<MockProtocolSvc>>();
    return mock;
}

ProtocolHandler* SharedProtocolHandler()
{
    return ProtocolHandler::Instance(SharedMockProtocolSvc());
}

class ProtocolHandlerDetailCfgSuite : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mock_ = SharedMockProtocolSvc();
        testing::Mock::VerifyAndClearExpectations(mock_.get());
        handler_ = SharedProtocolHandler();
    }

    void TearDown() override
    {
        testing::Mock::VerifyAndClearExpectations(mock_.get());
    }

    std::shared_ptr<testing::NiceMock<MockProtocolSvc>> mock_;
    ProtocolHandler *handler_{nullptr};
};

} // namespace

/*
测试思路：
1. 构造 DetailCfg 请求，但 cfg_data 传数组而不是 object。
2. 直接调用 ProtocolHandler::DetailCfg。
3. 断言 handler 在入参校验处返回 request param error，并且不会调用 service。

示意：
  HTTP body cfg_data = ["bad"]
           |
           v
  is_object() == false -> return -100

举例：
  前端误传 cfg_data: [] 时，不应继续读 DB，也不应触碰运行态 server。
*/
TEST_F(ProtocolHandlerDetailCfgSuite, RejectsNonObjectCfgDataBeforeServiceAndRuntime)
{
    kit_app::Application app(nullptr);
    handler_->SetApp(&app);

    EXPECT_CALL(*mock_, GetCfgById(testing::_, testing::_)).Times(0);
    EXPECT_CALL(*mock_, UpdateCfg(testing::_, testing::_, testing::_,
                                  testing::A<const std::string&>())).Times(0);
    EXPECT_CALL(*mock_, UpdateCfg(testing::_, testing::_, testing::_,
                                  testing::A<const nljson&>())).Times(0);

    auto ctx = MakeJsonContext(nljson{
        {"id", 1001},
        {"project_id", 9101},
        {"req_or_resp", kReqCfg},
        {"cfg_data", nljson::array({"bad"})},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -100);
    EXPECT_EQ(resp["message"], "request param error");
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
    kit_app::Application app(nullptr);
    app.AddServer(project_id, server);
    handler_->SetApp(&app);

    EXPECT_CALL(*mock_, UpdateCfg(testing::_, testing::_, testing::_,
                                  testing::A<const nljson&>())).Times(0);
    {
        testing::InSequence seq;
        EXPECT_CALL(*mock_, GetCfgById(testing::_, protocol_id))
            .WillOnce(testing::Return(nljson{
                {"req_cfg", old_req_cfg},
                {"resp_cfg", old_resp_cfg},
            }));
        EXPECT_CALL(*mock_, UpdateCfg(testing::_, protocol_id, kReqCfg, JsonStringEq(new_req_cfg)))
            .WillOnce(testing::Return(true));
    }

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", project_id},
        {"req_or_resp", kReqCfg},
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
    kit_app::Application app(nullptr);
    app.AddServer(project_id, server);
    handler_->SetApp(&app);

    EXPECT_CALL(*mock_, UpdateCfg(testing::_, testing::_, testing::_,
                                  testing::A<const nljson&>())).Times(0);
    {
        testing::InSequence seq;
        EXPECT_CALL(*mock_, GetCfgById(testing::_, protocol_id))
            .WillOnce(testing::Return(nljson{
                {"req_cfg", old_req_cfg},
                {"resp_cfg", old_resp_cfg},
            }));
        EXPECT_CALL(*mock_, UpdateCfg(testing::_, protocol_id, kReqCfg, JsonStringEq(new_req_cfg)))
            .WillOnce(testing::Return(true));
        EXPECT_CALL(*mock_, UpdateCfg(testing::_, protocol_id, kReqCfg, JsonStringEq(old_req_cfg)))
            .WillOnce(testing::Return(true));
    }

    auto ctx = MakeJsonContext(nljson{
        {"id", protocol_id},
        {"project_id", project_id},
        {"req_or_resp", kReqCfg},
        {"cfg_data", nljson{{"path", "/d9/web/conflict"}}},
    });

    handler_->DetailCfg(nullptr, ctx);

    auto resp = ResponseBody(ctx);
    EXPECT_EQ(resp["code"], -300);
    EXPECT_EQ(resp["message"], "protocolitem update cfg failed");

    auto runtime_item = GetHttpRuntimeItem(server, protocol_id);
    ASSERT_NE(runtime_item, nullptr);
    EXPECT_EQ(runtime_item->getReqCfg().path, "/d9/web/old");
    EXPECT_EQ(runtime_item->getReqCfg().headers.at("X-Old"), "1");
}
