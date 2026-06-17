/**
 * @file test_web_project.cpp
 * @brief ProjectHandler 参数化单元测试样板
 *
 * 本文件使用 GoogleTest 参数化测试（TEST_P + INSTANTIATE_TEST_SUITE_P）
 * 表达 C++ 里的“表格驱动测试”。
 *
 * 写法约定：
 * 1. 每个 case 都是独立 gtest 测试项，失败后不会阻断其他 case。
 * 2. case.id 使用英文、数字、下划线，作为 --gtest_filter 可过滤的测试名后缀。
 * 3. case.desc 使用中文，放入 SCOPED_TRACE，失败时用于解释业务场景。
 * 4. ProjectSvc 的 EXPECT_CALL 全部藏在 expect_svc factory 中。
 * 5. RuntimeController 的 EXPECT_CALL 全部藏在 expect_runtime factory 中。
 * 6. 主测试流程固定为 Arrange -> Act -> Assert，后续新增用例只需要追加 case。
 * 7. Handler 单元测试验证参数解析、鉴权、响应映射和 runtime controller 调用；
 *    ProjectRuntimeManager 的真实运行态行为放到 tests/work/runtime/test_project_runtime_manager.cpp。
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/log.h"
#include "base/time_stamp.h"
#include "domain/project.h"
#include "domain/project_server.h"
#include "domain/runtime_result.h"
#include "domain/type.h"
#include "domain/user.h"
#include "net/event_loop.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_server.h"
#include "net/inet_address.h"
#include "net/tcp_server.h"
#include "runtime/runtime_controller.h"
#include "service/mock/svc_project_mock.h"
#include "service/mock/svc_protocol_mock.h"
#include "web/web_project.h"
#include "work/runtime/mock/runtime_controller_mock.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace testing;

namespace {

using nljson = nlohmann::json;

constexpr int64_t kCurrentUserId = 1;
constexpr int64_t kOtherUserId = 2;
constexpr int64_t kProjectId = 9501;
constexpr int64_t kRuntimeProjectId = 9401;
constexpr int64_t kFixedTimeMs = 1710000000000LL;
constexpr uint16_t kRuntimeListenPort = 18080;
constexpr const char *kCurrentUserName = "web_project_tester";

struct AddProjectReq {
    std::string name;
    ProjectMode mode;
    ProtocolType protocol_type;
    std::string target_ip;
    nljson pattern_info;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddProjectReq, name, mode, protocol_type, target_ip, pattern_info)
};

struct ProjectListReq {
    int32_t offset;
    int32_t limit;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectListReq, offset, limit)
};

struct ProjectDetailNameReq {
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectDetailNameReq, name)
};

struct ProjectEditPatternInfoReq {
    int64_t id;
    nljson pattern_info;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectEditPatternInfoReq, id, pattern_info)
};


using ExpectProjectSvc = std::function<std::shared_ptr<MockProjectSvc>()>;
using ExpectRuntime = std::function<std::shared_ptr<MockRuntimeController>()>;
using BuildContext = std::function<HttpContextPtr()>;
using InvokeHandler = std::function<void(ProjectHandler &, HttpContextPtr)>;
using AssertResponse = std::function<void(HttpContextPtr)>;

static CurrentUser NormalUser(int64_t user_id = kCurrentUserId)
{
    return CurrentUser{user_id, kCurrentUserName, UserRole::kNormal, UserStatus::kActive};
}

static CurrentUser AdminUser(int64_t user_id = 100)
{
    return CurrentUser{user_id, "admin_user", UserRole::kAdmin, UserStatus::kActive};
}

static HttpContextPtr MakeContext(CurrentUser user = NormalUser())
{
    auto ctx = std::make_shared<HttpContext>();
    SetCurrentUserToContext(ctx, std::move(user));
    return ctx;
}

static HttpContextPtr MakeAnonymousContext()
{
    return std::make_shared<HttpContext>();
}

static void SetRequestBase(HttpRequestPtr req, int32_t method, const std::string &path)
{
    req->setVersion(Version::kHttp11);
    req->setMethod(method);
    req->setPath(path);
    req->addHeader("Content-Type", "application/json");
}

template<typename T>
static void SetJsonBody(HttpRequestPtr req, int32_t method, const std::string &path, T data)
{
    SetRequestBase(req, method, path);

    Body body((ContentType(ContentType::kJsonType)));
    nljson root = std::move(data);
    body.appendData(root.dump());
    req->setBody(body);
}

static void SetRawBody(HttpRequestPtr req,
                       int32_t method,
                       const std::string &path,
                       ContentType content_type,
                       const std::string &body_data)
{
    req->setVersion(Version::kHttp11);
    req->setMethod(method);
    req->setPath(path);
    req->addHeader("Content-Type", content_type.toString());

    Body body(content_type);
    body.appendData(body_data);
    req->setBody(body);
}

static Project MakeProject(int64_t project_id,
                           int64_t user_id = kCurrentUserId,
                           ProtocolType protocol_type = ProtocolType::kHttp,
                           ProjectStatus status = ProjectStatus::kValid,
                           ProjectRuntimeState runtime_state = ProjectRuntimeState::kStopped,
                           nljson pattern_info = nljson::object())
{
    Project p;
    p.m_id = project_id;
    p.m_name = "project_" + std::to_string(project_id);
    p.m_mode = ProjectMode::ServerMode;
    p.m_protocolType = protocol_type;
    p.m_listenPort = 0;
    p.m_targetIp = "";
    p.m_userId = user_id;
    p.m_status = status;
    p.m_runtimeState = runtime_state;
    p.m_patternInfo = std::move(pattern_info);
    p.m_ctime = TimeStamp(kFixedTimeMs);
    return p;
}

static nljson MinimalCustomTcpPatternInfo()
{
    return nljson::parse(R"({
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

static HttpContextPtr MakeAddProjectJsonContext(const AddProjectReq &request,
                                                CurrentUser user = NormalUser())
{
    auto ctx = MakeContext(std::move(user));
    SetJsonBody(ctx->request(), HttpRequest::Method::kPost, "/projects/add", request);
    return ctx;
}

static HttpContextPtr MakeProjectRouteContext(int32_t method,
                                              const std::string &path,
                                              const std::string &project_id_route,
                                              CurrentUser user = NormalUser())
{
    auto ctx = MakeContext(std::move(user));
    auto req = ctx->request();
    SetRequestBase(req, method, path);
    req->addRouteParam("project_id", project_id_route);
    return ctx;
}

static HttpContextPtr MakeRuntimeStateContext(const std::string &project_id_route,
                                              const std::string &operation)
{
    auto ctx = MakeProjectRouteContext(
        HttpRequest::Method::kPost,
        "/projects/" + project_id_route + "/runtime_state",
        project_id_route);
    ctx->request()->addQureyParam("operation", operation);
    return ctx;
}

static HttpContextPtr MakeListContext(const ProjectListReq &request, CurrentUser user = NormalUser())
{
    auto ctx = MakeContext(std::move(user));
    SetJsonBody(ctx->request(), HttpRequest::Method::kPost, "/projects/list", request);
    return ctx;
}

static HttpContextPtr MakeDetailNameContext(const std::string &project_id_route,
                                            const ProjectDetailNameReq &request)
{
    auto ctx = MakeProjectRouteContext(
        HttpRequest::Method::kPost,
        "/projects/" + project_id_route + "/name",
        project_id_route);
    SetJsonBody(ctx->request(),
        HttpRequest::Method::kPost,
        "/projects/" + project_id_route + "/name",
        request);
    ctx->request()->addRouteParam("project_id", project_id_route);
    return ctx;
}

static HttpContextPtr MakeEditPatternInfoContext(const ProjectEditPatternInfoReq &request)
{
    auto ctx = MakeContext();
    SetJsonBody(ctx->request(), HttpRequest::Method::kPost, "/projects/pattern_info", request);
    return ctx;
}

static nljson ParseJsonBody(HttpContextPtr ctx)
{
    return nljson::parse(ctx->response()->body().toString());
}

static void ExpectJsonResponse(HttpContextPtr ctx, int32_t state_code, const nljson &expected)
{
    ASSERT_EQ(ctx->response()->stateCode().toInt(), state_code);
    EXPECT_EQ(ParseJsonBody(ctx), expected);
}

static nljson WriteBody(int32_t code,
                        const std::string &message,
                        int32_t persisted,
                        int32_t runtime_applied)
{
    return nljson{
        {"code", code},
        {"message", message},
        {"data", {
            {"persisted", persisted},
            {"runtime_applied", runtime_applied},
        }},
    };
}

static std::string ProjectLengthPolicy(const Project &p)
{
    if(p.m_protocolType != ProtocolType::kCustomTcp || !p.m_patternInfo.is_object())
    {
        return "";
    }

    auto iter = p.m_patternInfo.find("length_policy");
    return iter == p.m_patternInfo.end() || !iter->is_string() ? "" : iter->get<std::string>();
}

static nljson ProjectVoBody(const Project &p)
{
    return nljson{
        {"id", p.m_id},
        {"name", p.m_name},
        {"mode", static_cast<int32_t>(p.m_mode)},
        {"protocol_type", static_cast<int32_t>(p.m_protocolType)},
        {"length_policy", ProjectLengthPolicy(p)},
        {"listen_port", p.m_listenPort},
        {"target_ip", p.m_targetIp},
        {"user_id", p.m_userId},
        {"status", static_cast<int32_t>(p.m_status)},
        {"runtime_state", static_cast<int32_t>(p.m_runtimeState)},
        {"ctime", p.m_ctime.toString()},
    };
}

static nljson ProjectArrayResponse(const std::vector<Project> &projects,
                                   const std::string &message = "success")
{
    nljson root;
    root["code"] = 0;
    root["message"] = message;
    root["data"] = nljson::array();
    for(const auto &p : projects)
    {
        root["data"].push_back(ProjectVoBody(p));
    }
    return root;
}

static ProjectRuntimeSnapshot RuntimeSnapshot(int64_t project_id,
                                              ProjectRuntimeState state,
                                              uint16_t listen_port)
{
    ProjectRuntimeSnapshot snapshot;
    snapshot.project_id = project_id;
    snapshot.runtime_state = state;
    snapshot.listen_port = listen_port;
    return snapshot;
}

static ProjectRuntimeResult RuntimeSuccess(int64_t project_id,
                                           ProjectRuntimeState state,
                                           uint16_t listen_port)
{
    return ProjectRuntimeResult::Success(
        RuntimeMutationReceipt::AllOk(),
        RuntimeSnapshot(project_id, state, listen_port));
}

static ProjectRuntimeResult RuntimeFailure(RuntimeControlCode code,
                                           const std::string &message,
                                           RuntimeMutationReceipt receipt = RuntimeMutationReceipt::AllErr(),
                                           ProjectRuntimeSnapshot snapshot = ProjectRuntimeSnapshot{})
{
    return ProjectRuntimeResult::Failed(code,
        RuntimeError(RuntimeError::kInternalError),
        message,
        receipt,
        snapshot);
}

static std::shared_ptr<MockProjectSvc> ExpectNoProjectSvcCalls()
{
    return std::make_shared<StrictMock<MockProjectSvc>>();
}

static std::shared_ptr<MockRuntimeController> ExpectNoRuntimeCalls()
{
    return std::make_shared<StrictMock<MockRuntimeController>>();
}

static std::shared_ptr<MockProtocolSvc> ExpectNoProtocolSvcCalls()
{
    return std::make_shared<StrictMock<MockProtocolSvc>>();
}

struct HandlerCase {
    std::string id;    // gtest 参数化测试名后缀。
    std::string desc;  // 中文业务说明，失败时由 SCOPED_TRACE 输出。

    BuildContext build_ctx = [] {
        return MakeContext();
    };

    // 把 ProjectSvc 的 EXPECT_CALL 全部藏进 factory。
    // 主测试流程只拿“已经设置好预期的 mock”。
    ExpectProjectSvc expect_svc = [] {
        return ExpectNoProjectSvcCalls();
    };

    // RuntimeController 也是同样的 factory 形态。
    // 不涉及 runtime 的 handler 默认使用 StrictMock，确保不会误调用。
    ExpectRuntime expect_runtime = [] {
        return ExpectNoRuntimeCalls();
    };

    InvokeHandler invoke;
    AssertResponse assert_response;
};

static HandlerCase Case(std::string id,
                        std::string desc,
                        BuildContext build_ctx,
                        ExpectProjectSvc expect_svc,
                        ExpectRuntime expect_runtime,
                        InvokeHandler invoke,
                        AssertResponse assert_response)
{
    HandlerCase c;
    c.id = std::move(id);
    c.desc = std::move(desc);
    c.build_ctx = std::move(build_ctx);
    c.expect_svc = std::move(expect_svc);
    c.expect_runtime = std::move(expect_runtime);
    c.invoke = std::move(invoke);
    c.assert_response = std::move(assert_response);
    return c;
}

static HandlerCase Case(std::string id,
                        std::string desc,
                        BuildContext build_ctx,
                        ExpectProjectSvc expect_svc,
                        InvokeHandler invoke,
                        AssertResponse assert_response)
{
    return Case(std::move(id),
        std::move(desc),
        std::move(build_ctx),
        std::move(expect_svc),
        [] {
            return ExpectNoRuntimeCalls();
        },
        std::move(invoke),
        std::move(assert_response));
}

static void RunHandlerCase(const HandlerCase &c)
{
    SCOPED_TRACE(c.desc);

    auto svc = c.expect_svc();
    auto pc_svc = ExpectNoProtocolSvcCalls();
    auto runtime = c.expect_runtime();
    ProjectHandler handler(svc, pc_svc, runtime);
    auto ctx = c.build_ctx();

    c.invoke(handler, ctx);

    c.assert_response(ctx);
}

static void InvokeAddProject(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.AddProject(nullptr, ctx);
}

static void InvokeRuntimeState(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.StartAndStopProject(nullptr, ctx);
}

static void InvokeDelProject(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.DelProject(nullptr, ctx);
}

static void InvokeRestoreProject(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.RestoreProject(nullptr, ctx);
}

static void InvokeSingleProject(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.SingleProject(nullptr, ctx);
}

static void InvokeList(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.List(nullptr, ctx);
}

static void InvokeGetAllValid(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.GetAllValid(nullptr, ctx);
}

static void InvokeDetailName(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.DetailName(nullptr, ctx);
}

static void InvokeQueryPatternInfo(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.QueryPatternInfo(nullptr, ctx);
}

static void InvokeEditPatternInfo(ProjectHandler &handler, HttpContextPtr ctx)
{
    handler.EditPatternInfo(nullptr, ctx);
}

class ProjectHandlerParamTest
    : public Test
    , public WithParamInterface<HandlerCase>
{
protected:
    void SetUp() override
    {
        KIT_LOGGER("net")->setLevel(LogLevel::ERROR);
        KIT_LOGGER("base")->setLevel(LogLevel::ERROR);
        KIT_LOGGER("web")->setLevel(LogLevel::ERROR);
        KIT_LOGGER("domain")->setLevel(LogLevel::ERROR);
    }
};

TEST_P(ProjectHandlerParamTest, HandlesCase)
{
    RunHandlerCase(GetParam());
}

static std::string HandlerCaseName(const TestParamInfo<HandlerCase> &info)
{
    return info.param.id;
}

static std::vector<HandlerCase> MakeAddProjectCases()
{
    return {
        /*
        测试思路：
        1. 请求体符合 AddProjectReq JSON schema。
        2. 当前用户来自 HttpContext，handler 会把 user_id 写入 Project。
        3. ProjectSvc::Add 返回 project_id=1。

        示例：
          POST /projects/add
          body.name=test1, protocol_type=http
              |
              v
          Add(ctx, Project{user_id=1}) -> 1
              |
              v
          {"code":0,"data":{"project_id":1,"persisted":1,"runtime_applied":0}}
        */
        Case("Success",
            "新增 HTTP project 成功：service 返回 project_id，响应写入项目 id 和写操作回执。",
            [] {
                return MakeAddProjectJsonContext(AddProjectReq{
                    "test1",
                    ProjectMode::ServerMode,
                    ProtocolType::kHttp,
                    "",
                    nljson::object(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, Add(_, _))
                    .WillOnce(DoAll(
                        WithArg<1>([](const Project &p) {
                            EXPECT_EQ(p.m_id, -1);
                            EXPECT_EQ(p.m_name, "test1");
                            EXPECT_EQ(p.m_userId, kCurrentUserId);
                            EXPECT_EQ(p.m_protocolType, ProtocolType::kHttp);
                            EXPECT_EQ(p.m_runtimeState, ProjectRuntimeState::kStopped);
                        }),
                        Return(1)));
                return svc;
            },
            InvokeAddProject,
            [](HttpContextPtr ctx) {
                auto expected = WriteBody(0, "success", 1, 0);
                expected["data"]["project_id"] = 1;
                ExpectJsonResponse(ctx, StateCode::k200Ok, expected);
            }),

        /*
        测试思路：
        1. 请求 Content-Type 为 application/xml。
        2. HttpContext::Bind 不支持 XML 绑定 AddProjectReq。
        3. handler 直接返回 body parse error，ProjectSvc 不应被调用。

        示例：
          XML body -> Bind=false -> {"code":-200,"message":"body parse error"}
        */
        Case("RejectXmlBody",
            "拒绝 XML body：Bind 失败，不进入 ProjectSvc::Add。",
            [] {
                auto ctx = MakeContext();
                SetRawBody(ctx->request(),
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    ContentType(ContentType::kXmlType),
                    "<project><name>test1</name></project>");
                return ctx;
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeAddProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "body parse error", 0, 0));
            }),

        /*
        测试思路：
        1. JSON 解析成功，但 protocol_type 映射为 ProtocolType::kUnknown。
        2. CheckProjectInfo 在 service 前失败。
        3. ProjectSvc::Add 不应被调用。

        示例：
          protocol_type=UNKNOWN -> {"code":-200,"message":"project info invalid"}
        */
        Case("RejectInvalidProtocolType",
            "拒绝非法协议类型：业务参数校验失败，不进入 ProjectSvc::Add。",
            [] {
                return MakeAddProjectJsonContext(AddProjectReq{
                    "bad_type",
                    ProjectMode::ServerMode,
                    ProtocolType::kUnknown,
                    "",
                    nljson::object(),
                });
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeAddProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "project info invalid", 0, 0));
            }),

        /*
        测试思路：
        1. 请求体合法，但 HttpContext 中没有当前登录用户。
        2. handler 在写 DB 前做用户校验。
        3. 缺少 user_id 时返回 HTTP 403，ProjectSvc 不应被调用。

        示例：
          current_user.user_id=0 -> HTTP 403 forbidden
        */
        Case("ForbiddenWithoutCurrentUser",
            "新增 project 时缺少当前用户：返回 403，不进入 ProjectSvc::Add。",
            [] {
                auto ctx = MakeAnonymousContext();
                SetJsonBody(ctx->request(),
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    AddProjectReq{
                        "test1",
                        ProjectMode::ServerMode,
                        ProtocolType::kHttp,
                        "",
                        nljson::object(),
                    });
                return ctx;
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeAddProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 请求体和用户校验都通过。
        2. ProjectSvc::Add 返回 -1，模拟持久化失败。
        3. handler 统一转换为 service failed。

        示例：
          Add(...) -> -1 -> {"code":-300,"message":"service failed"}
        */
        Case("ServiceAddFailed",
            "ProjectSvc::Add 返回失败：handler 返回 service failed。",
            [] {
                return MakeAddProjectJsonContext(AddProjectReq{
                    "test1",
                    ProjectMode::ServerMode,
                    ProtocolType::kHttp,
                    "",
                    nljson::object(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, Add(_, _))
                    .WillOnce(Return(-1));
                return svc;
            },
            InvokeAddProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-300, "service failed", 0, 0));
            }),
    };
}

static std::vector<HandlerCase> MakeRuntimeStateCases()
{
    return {
        /*
        测试思路：
        1. operation=running，表示启动 project runtime。
        2. 鉴权通过后，handler 调用 RuntimeController::startProject。
        3. 响应里透出 listen_port 和 runtime_state。

        示例：
          POST /projects/9401/runtime_state?operation=1
              |
              v
          startProject(9401) -> running:18080
              |
              v
          {"runtime_state":1,"listen_port":18080}
        */
        Case("StartSuccess",
            "启动 project 成功：handler 调用 startProject 并返回监听端口。",
            [] {
                return MakeRuntimeStateContext(
                    std::to_string(kRuntimeProjectId),
                    std::to_string(static_cast<int32_t>(ProjectRuntimeState::kRunning)));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kRuntimeProjectId))
                    .WillOnce(Return(MakeProject(kRuntimeProjectId)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, startProject(_, kRuntimeProjectId))
                    .WillOnce(Return(RuntimeSuccess(
                        kRuntimeProjectId,
                        ProjectRuntimeState::kRunning,
                        kRuntimeListenPort)));
                return runtime;
            },
            InvokeRuntimeState,
            [](HttpContextPtr ctx) {
                auto expected = WriteBody(0, "success", 1, 1);
                expected["data"]["listen_port"] = kRuntimeListenPort;
                expected["data"]["runtime_state"] = static_cast<int32_t>(ProjectRuntimeState::kRunning);
                ExpectJsonResponse(ctx, StateCode::k200Ok, expected);
            }),

        /*
        测试思路：
        1. operation=stopped，表示停止 project runtime。
        2. 鉴权通过后，handler 调用 RuntimeController::stopProject。
        3. 响应 listen_port=0，runtime_state=stopped。

        示例：
          operation=0 -> stopProject(9401) -> {"runtime_state":0,"listen_port":0}
        */
        Case("StopSuccess",
            "停止 project 成功：handler 调用 stopProject 并返回 stopped 快照。",
            [] {
                return MakeRuntimeStateContext(
                    std::to_string(kRuntimeProjectId),
                    std::to_string(static_cast<int32_t>(ProjectRuntimeState::kStopped)));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kRuntimeProjectId))
                    .WillOnce(Return(MakeProject(kRuntimeProjectId)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, stopProject(_, kRuntimeProjectId))
                    .WillOnce(Return(RuntimeSuccess(
                        kRuntimeProjectId,
                        ProjectRuntimeState::kStopped,
                        0)));
                return runtime;
            },
            InvokeRuntimeState,
            [](HttpContextPtr ctx) {
                auto expected = WriteBody(0, "success", 1, 1);
                expected["data"]["listen_port"] = 0;
                expected["data"]["runtime_state"] = static_cast<int32_t>(ProjectRuntimeState::kStopped);
                ExpectJsonResponse(ctx, StateCode::k200Ok, expected);
            }),

        /*
        测试思路：
        1. route param project_id 不能转成正数，atoi 后为 0。
        2. handler 在鉴权和 runtime 调用前返回 request param invalid。
        3. ProjectSvc 和 RuntimeController 都不应被调用。

        示例：
          /projects/not-a-number/runtime_state?operation=1 -> request param invalid
        */
        Case("RejectInvalidProjectId",
            "启动/停止接口 project_id 非法：提前返回参数错误。",
            [] {
                return MakeRuntimeStateContext(
                    "not-a-number",
                    std::to_string(static_cast<int32_t>(ProjectRuntimeState::kRunning)));
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeRuntimeState,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "request param invalid", 0, 0));
            }),

        /*
        测试思路：
        1. 参数合法，但 ProjectSvc::GetById 返回其他用户的 project。
        2. 当前用户不是管理员，鉴权失败。
        3. RuntimeController 不应被调用。

        示例：
          current_user=1, project.user_id=2 -> HTTP 403 forbidden
        */
        Case("ForbiddenWhenProjectBelongsToOtherUser",
            "普通用户启动/停止别人的 project：鉴权失败。",
            [] {
                return MakeRuntimeStateContext(
                    std::to_string(kRuntimeProjectId),
                    std::to_string(static_cast<int32_t>(ProjectRuntimeState::kRunning)));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kRuntimeProjectId))
                    .WillOnce(Return(MakeProject(kRuntimeProjectId, kOtherUserId)));
                return svc;
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeRuntimeState,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 参数和鉴权都通过，但 RuntimeController::startProject 返回业务失败。
        2. RuntimeControlCode::kProjectTypeInvalid 映射为写响应 code=-200。
        3. handler 仍写入 runtime 快照字段，前端可以看到当前状态。

        示例：
          startProject -> Failed(kProjectTypeInvalid)
              |
              v
          {"code":-200,"runtime_state":0,"listen_port":0}
        */
        Case("StartRuntimeRejectedByProjectType",
            "运行态启动失败且错误属于参数类：handler 映射为 code=-200。",
            [] {
                return MakeRuntimeStateContext(
                    std::to_string(kRuntimeProjectId),
                    std::to_string(static_cast<int32_t>(ProjectRuntimeState::kRunning)));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kRuntimeProjectId))
                    .WillOnce(Return(MakeProject(kRuntimeProjectId)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, startProject(_, kRuntimeProjectId))
                    .WillOnce(Return(RuntimeFailure(
                        RuntimeControlCode::kProjectTypeInvalid,
                        "project type invalid",
                        RuntimeMutationReceipt::AllErr(),
                        RuntimeSnapshot(kRuntimeProjectId, ProjectRuntimeState::kStopped, 0))));
                return runtime;
            },
            InvokeRuntimeState,
            [](HttpContextPtr ctx) {
                auto expected = WriteBody(-200, "project type invalid", 0, 0);
                expected["data"]["listen_port"] = 0;
                expected["data"]["runtime_state"] = static_cast<int32_t>(ProjectRuntimeState::kStopped);
                ExpectJsonResponse(ctx, StateCode::k200Ok, expected);
            }),
    };
}

static std::vector<HandlerCase> MakeDelProjectCases()
{
    return {
        /*
        测试思路：
        1. route param 中带合法 project_id。
        2. ProjectSvc::GetById 返回当前用户拥有的 project。
        3. RuntimeController::delProject 返回成功，handler 只做响应转换。

        示例：
          DELETE /projects/9501
              |
              v
          GetById(owner=1) -> delProject(9501) -> {"code":0}
        */
        Case("Success",
            "软删除当前用户自己的 project：鉴权通过，委托 RuntimeController::delProject。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kDelete,
                    "/projects/" + std::to_string(kProjectId),
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, delProject(_, kProjectId))
                    .WillOnce(Return(ProjectRuntimeResult::Success(
                        RuntimeMutationReceipt::AllOk(),
                        RuntimeSnapshot(kProjectId, ProjectRuntimeState::kStopped, 0))));
                return runtime;
            },
            InvokeDelProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(0, "success", 1, 1));
            }),

        /*
        测试思路：
        1. route param 缺少 project_id。
        2. handler 在 std::stol 前返回参数错误。
        3. ProjectSvc 和 RuntimeController 都不应被调用。

        示例：
          DELETE /projects/ -> {"code":-200,"message":"query param parse error"}
        */
        Case("RejectEmptyProjectId",
            "删除接口缺少 project_id：提前返回 query param parse error。",
            [] {
                auto ctx = MakeContext();
                SetRequestBase(ctx->request(), HttpRequest::Method::kDelete, "/projects/");
                return ctx;
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeDelProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "query param parse error", 0, 0));
            }),

        /*
        测试思路：
        1. route param 不是数字，std::stol 抛异常。
        2. handler catch 后返回 service failed。
        3. ProjectSvc 和 RuntimeController 都不应被调用。

        示例：
          DELETE /projects/not-a-number -> {"code":-300,"message":"service failed"}
        */
        Case("RejectNonNumericProjectId",
            "删除接口 route param 不是数字：不访问 service/runtime，返回 service failed。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kDelete,
                    "/projects/not-a-number",
                    "not-a-number");
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeDelProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-300, "service failed", 0, 0));
            }),

        /*
        测试思路：
        1. ProjectSvc::GetById 返回其他用户拥有的 project。
        2. 当前用户不是管理员，鉴权失败。
        3. RuntimeController::delProject 不应被调用。

        示例：
          current_user=1, project.user_id=2 -> HTTP 403 forbidden
        */
        Case("ForbiddenWhenProjectBelongsToOtherUser",
            "普通用户删除别人的 project：鉴权失败，runtime 不应被调用。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kDelete,
                    "/projects/" + std::to_string(kProjectId),
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId, kOtherUserId)));
                return svc;
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeDelProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 鉴权通过，但 RuntimeController::delProject 返回持久化失败。
        2. WriteOpResult::FromPjRuntimeResult 把 RuntimeControlCode::kPersistFailed 映射为 -300。
        3. 响应中 persisted/runtime_applied 都为 0。

        示例：
          delProject -> Failed(kPersistFailed, "persist failed")
              |
              v
          {"code":-300,"message":"persist failed"}
        */
        Case("RuntimeDeleteFailed",
            "运行态控制器返回删除失败：handler 映射为写操作失败响应。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kDelete,
                    "/projects/" + std::to_string(kProjectId),
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, delProject(_, kProjectId))
                    .WillOnce(Return(RuntimeFailure(RuntimeControlCode::kPersistFailed, "persist failed")));
                return runtime;
            },
            InvokeDelProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-300, "persist failed", 0, 0));
            }),
    };
}

static std::vector<HandlerCase> MakeRestoreProjectCases()
{
    return {
        /*
        测试思路：
        1. RestoreProject 是管理员操作，普通用户不能恢复软删除 project。
        2. 管理员传入合法 project_id 后，handler 先恢复 status，再重置 runtime_state。
        3. 两个 service 更新都成功时，返回 persisted=1/runtime_applied=0。

        示例：
          admin POST /projects/9501/restore
              |
              v
          UpdateStatus(valid) && UpdateRuntimeState(stopped,0) -> success
        */
        Case("AdminSuccess",
            "管理员恢复 project：同时恢复有效状态并重置运行态字段。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kPost,
                    "/projects/" + std::to_string(kProjectId) + "/restore",
                    std::to_string(kProjectId),
                    AdminUser());
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, UpdateStatus(_, kProjectId, ProjectStatus::kValid))
                    .WillOnce(Return(true));
                EXPECT_CALL(*svc, UpdateRuntimeState(_, kProjectId, ProjectRuntimeState::kStopped, 0))
                    .WillOnce(Return(true));
                return svc;
            },
            InvokeRestoreProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(0, "success", 1, 0));
            }),

        /*
        测试思路：
        1. 当前用户不是管理员。
        2. handler 在解析 project_id 和调用 service 前返回 403。
        3. ProjectSvc 不应被调用。

        示例：
          normal user -> HTTP 403 forbidden
        */
        Case("RejectNormalUser",
            "普通用户恢复 project：管理员校验失败。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kPost,
                    "/projects/" + std::to_string(kProjectId) + "/restore",
                    std::to_string(kProjectId));
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeRestoreProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 管理员身份通过，但 route param 不是数字。
        2. std::stol 抛异常，handler 返回 query param fail。
        3. ProjectSvc 不应被调用。

        示例：
          admin POST /projects/not-a-number/restore -> {"code":-200}
        */
        Case("RejectInvalidProjectId",
            "管理员恢复 project 但 project_id 非数字：返回 query param fail。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kPost,
                    "/projects/not-a-number/restore",
                    "not-a-number",
                    AdminUser());
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeRestoreProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "query param fail", 0, 0));
            }),

        /*
        测试思路：
        1. 管理员身份和 project_id 都合法。
        2. UpdateStatus 返回 false，&& 短路，因此 UpdateRuntimeState 不应被调用。
        3. handler 返回 service failed。

        示例：
          UpdateStatus -> false -> {"code":-300,"message":"service failed"}
        */
        Case("ServiceUpdateStatusFailed",
            "恢复 project 时状态更新失败：返回 service failed，并且不继续重置 runtime_state。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kPost,
                    "/projects/" + std::to_string(kProjectId) + "/restore",
                    std::to_string(kProjectId),
                    AdminUser());
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, UpdateStatus(_, kProjectId, ProjectStatus::kValid))
                    .WillOnce(Return(false));
                return svc;
            },
            InvokeRestoreProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-300, "service failed", 0, 0));
            }),
    };
}

static std::vector<HandlerCase> MakeSingleProjectCases()
{
    return {
        /*
        测试思路：
        1. route param 中带合法 project_id。
        2. ProjectSvc::GetById 返回当前用户拥有的 project。
        3. handler 把领域对象转换为 ProjectVo 数组返回。

        示例：
          GET /projects/9501 -> {"code":0,"data":[ProjectVo]}
        */
        Case("Success",
            "查询单个 project 成功：返回 ProjectVo 数组。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/" + std::to_string(kProjectId),
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                return svc;
            },
            InvokeSingleProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    ProjectArrayResponse({MakeProject(kProjectId)}));
            }),

        /*
        测试思路：
        1. ProjectSvc::GetById 返回 m_id<=0，表示 project 不存在。
        2. handler 当前约定不是 404，而是 code=0/message=project is not exists。
        3. data 返回空数组。

        示例：
          GetById -> Project{m_id=0} -> {"message":"project is not exists!","data":[]}
        */
        Case("ProjectNotFound",
            "查询不存在的 project：按当前接口约定返回 code=0 和空数组。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/" + std::to_string(kProjectId),
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(0)));
                return svc;
            },
            InvokeSingleProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", 0}, {"message", "project is not exists!"}, {"data", nljson::array()}});
            }),

        /*
        测试思路：
        1. ProjectSvc::GetById 返回其他用户拥有的 project。
        2. 当前用户不是管理员，鉴权失败。
        3. handler 返回 HTTP 403。

        示例：
          current_user=1, project.user_id=2 -> HTTP 403 forbidden
        */
        Case("ForbiddenWhenProjectBelongsToOtherUser",
            "普通用户查询别人的 project：返回 403。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/" + std::to_string(kProjectId),
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId, kOtherUserId)));
                return svc;
            },
            InvokeSingleProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. route param 不是数字，std::stol 抛异常。
        2. handler 返回 query param transform fail。
        3. ProjectSvc 不应被调用。

        示例：
          GET /projects/not-a-number -> {"code":-200,"message":"query param transform fail"}
        */
        Case("RejectInvalidProjectId",
            "查询单个 project 时 project_id 非数字：提前返回参数转换失败。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/not-a-number",
                    "not-a-number");
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeSingleProject,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", -200}, {"message", "query param transform fail"}});
            }),
    };
}

static std::vector<HandlerCase> MakeListCases()
{
    return {
        /*
        测试思路：
        1. 普通用户请求 /projects/list。
        2. handler 按 current_user.user_id 调用 ProjectSvc::GetByUser，只查询有效 project。
        3. 返回 ProjectVo 数组。

        示例：
          normal user -> GetByUser(user_id=1,status=valid,offset=5,limit=10)
        */
        Case("NormalUserSuccess",
            "普通用户分页查询 project：只查询自己的有效项目。",
            [] {
                return MakeListContext(ProjectListReq{5, 10});
            },
            [] {
                const std::vector<Project> projects{MakeProject(1001), MakeProject(1002)};
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetByUser(_, kCurrentUserId, ProjectStatus::kValid, 5, 10))
                    .WillOnce(Return(projects));
                return svc;
            },
            InvokeList,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    ProjectArrayResponse({MakeProject(1001), MakeProject(1002)}));
            }),

        /*
        测试思路：
        1. 管理员请求 /projects/list。
        2. handler 走 ProjectSvc::GetAll 分支，不按 user_id 过滤。
        3. 返回 ProjectVo 数组。

        示例：
          admin -> GetAll(offset=0,limit=20)
        */
        Case("AdminSuccess",
            "管理员分页查询 project：走 GetAll 分支。",
            [] {
                return MakeListContext(ProjectListReq{0, 20}, AdminUser());
            },
            [] {
                const std::vector<Project> projects{MakeProject(2001, kOtherUserId)};
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetAll(_, 0, 20))
                    .WillOnce(Return(projects));
                return svc;
            },
            InvokeList,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    ProjectArrayResponse({MakeProject(2001, kOtherUserId)}));
            }),

        /*
        测试思路：
        1. Content-Type 是 JSON，但 body 不是合法 JSON。
        2. HttpContext::Bind 返回 false。
        3. ProjectSvc 不应被调用。

        示例：
          body="{\"offset\":" -> {"code":-200,"message":"body parse error"}
        */
        Case("RejectBrokenJsonBody",
            "列表查询 body 不是合法 JSON：不进入 ProjectSvc。",
            [] {
                auto ctx = MakeContext();
                SetRawBody(ctx->request(),
                    HttpRequest::Method::kPost,
                    "/projects/list",
                    ContentType(ContentType::kJsonType),
                    R"({"offset":)");
                return ctx;
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeList,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", -200}, {"message", "body parse error"}});
            }),

        /*
        测试思路：
        1. body 解析成功。
        2. ProjectSvc::GetByUser 抛异常，模拟 service 层失败。
        3. handler 返回 service failed。

        示例：
          GetByUser throws -> {"code":-300,"message":"service failed"}
        */
        Case("ServiceListFailed",
            "普通用户列表查询时 service 抛异常：返回 service failed。",
            [] {
                return MakeListContext(ProjectListReq{0, 10});
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetByUser(_, kCurrentUserId, ProjectStatus::kValid, 0, 10))
                    .WillOnce(Throw(std::runtime_error("list failed")));
                return svc;
            },
            InvokeList,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", -300}, {"message", "service failed"}});
            }),
    };
}

static std::vector<HandlerCase> MakeGetAllValidCases()
{
    return {
        /*
        测试思路：
        1. 普通用户请求 /projects/valid。
        2. handler 固定使用 offset=0/limit=1000 调用 GetByUser。
        3. 返回当前用户有效项目。

        示例：
          normal user -> GetByUser(1, valid, 0, 1000)
        */
        Case("NormalUserSuccess",
            "普通用户查询全部有效 project：只返回自己的有效项目。",
            [] {
                auto ctx = MakeContext();
                SetRequestBase(ctx->request(), HttpRequest::Method::kGet, "/projects/valid");
                return ctx;
            },
            [] {
                const std::vector<Project> projects{MakeProject(3001)};
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetByUser(_, kCurrentUserId, ProjectStatus::kValid, 0, 1000))
                    .WillOnce(Return(projects));
                return svc;
            },
            InvokeGetAllValid,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    ProjectArrayResponse({MakeProject(3001)}));
            }),

        /*
        测试思路：
        1. 管理员请求 /projects/valid。
        2. handler 走 ProjectSvc::GetAllValid 分支。
        3. 返回所有有效项目。

        示例：
          admin -> GetAllValid()
        */
        Case("AdminSuccess",
            "管理员查询全部有效 project：走 GetAllValid 分支。",
            [] {
                auto ctx = MakeContext(AdminUser());
                SetRequestBase(ctx->request(), HttpRequest::Method::kGet, "/projects/valid");
                return ctx;
            },
            [] {
                const std::vector<Project> projects{MakeProject(3002, kOtherUserId)};
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetAllValid(_))
                    .WillOnce(Return(projects));
                return svc;
            },
            InvokeGetAllValid,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    ProjectArrayResponse({MakeProject(3002, kOtherUserId)}));
            }),

        /*
        测试思路：
        1. 普通用户分支调用 ProjectSvc::GetByUser。
        2. service 抛异常。
        3. handler 返回 service failed。

        示例：
          GetByUser throws -> {"code":-300,"message":"service failed"}
        */
        Case("ServiceGetAllValidFailed",
            "查询全部有效 project 时 service 抛异常：返回 service failed。",
            [] {
                auto ctx = MakeContext();
                SetRequestBase(ctx->request(), HttpRequest::Method::kGet, "/projects/valid");
                return ctx;
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetByUser(_, kCurrentUserId, ProjectStatus::kValid, 0, 1000))
                    .WillOnce(Throw(std::runtime_error("valid failed")));
                return svc;
            },
            InvokeGetAllValid,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", -300}, {"message", "service failed"}});
            }),
    };
}

static std::vector<HandlerCase> MakeDetailNameCases()
{
    return {
        /*
        测试思路：
        1. route param project_id 合法，且项目归当前用户所有。
        2. body 绑定 ProjectDetailNameReq 成功。
        3. handler 调用 ProjectSvc::UpdateName。

        示例：
          POST /projects/9501/name {"name":"new_name"}
              |
              v
          GetById(owner=1) -> UpdateName("new_name") -> success
        */
        Case("Success",
            "修改 project 名称成功：鉴权通过后调用 UpdateName。",
            [] {
                return MakeDetailNameContext(
                    std::to_string(kProjectId),
                    ProjectDetailNameReq{"new_name"});
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                EXPECT_CALL(*svc, UpdateName(_, kProjectId, "new_name"))
                    .WillOnce(Return(true));
                return svc;
            },
            InvokeDetailName,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(0, "success", 1, 0));
            }),

        /*
        测试思路：
        1. route param project_id 非数字。
        2. handler 在 service 调用前返回 query param fail。
        3. ProjectSvc 不应被调用。

        示例：
          POST /projects/not-a-number/name -> {"code":-200,"message":"query param  fail"}
        */
        Case("RejectInvalidProjectId",
            "修改名称时 project_id 非数字：提前返回 query param fail。",
            [] {
                return MakeDetailNameContext("not-a-number", ProjectDetailNameReq{"new_name"});
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            InvokeDetailName,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "query param  fail", 0, 0));
            }),

        /*
        测试思路：
        1. route param 合法，但 GetById 返回其他用户的项目。
        2. 当前用户不是管理员，鉴权失败。
        3. body 即使合法也不应调用 UpdateName。

        示例：
          owner=2,current_user=1 -> HTTP 403 forbidden
        */
        Case("ForbiddenWhenProjectBelongsToOtherUser",
            "普通用户修改别人的 project 名称：返回 403。",
            [] {
                return MakeDetailNameContext(
                    std::to_string(kProjectId),
                    ProjectDetailNameReq{"new_name"});
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId, kOtherUserId)));
                return svc;
            },
            InvokeDetailName,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 鉴权通过后，handler 才绑定 body。
        2. body 不是合法 JSON，Bind 返回 false。
        3. UpdateName 不应被调用。

        示例：
          GetById(owner=1) -> Bind=false -> {"code":-200,"message":"body parse error"}
        */
        Case("RejectBrokenJsonBody",
            "修改名称 body 解析失败：鉴权通过后返回 body parse error。",
            [] {
                auto ctx = MakeProjectRouteContext(
                    HttpRequest::Method::kPost,
                    "/projects/" + std::to_string(kProjectId) + "/name",
                    std::to_string(kProjectId));
                SetRawBody(ctx->request(),
                    HttpRequest::Method::kPost,
                    "/projects/" + std::to_string(kProjectId) + "/name",
                    ContentType(ContentType::kJsonType),
                    R"({"name":)");
                ctx->request()->addRouteParam("project_id", std::to_string(kProjectId));
                return ctx;
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                return svc;
            },
            InvokeDetailName,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "body parse error", 0, 0));
            }),

        /*
        测试思路：
        1. 参数、鉴权、body 绑定都通过。
        2. ProjectSvc::UpdateName 返回 false。
        3. handler 返回 service failed。

        示例：
          UpdateName -> false -> {"code":-300,"message":"service failed"}
        */
        Case("ServiceUpdateNameFailed",
            "修改名称时 service 更新失败：返回 service failed。",
            [] {
                return MakeDetailNameContext(
                    std::to_string(kProjectId),
                    ProjectDetailNameReq{"new_name"});
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                EXPECT_CALL(*svc, UpdateName(_, kProjectId, "new_name"))
                    .WillOnce(Return(false));
                return svc;
            },
            InvokeDetailName,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-300, "service failed", 0, 0));
            }),
    };
}

static std::vector<HandlerCase> MakeQueryPatternInfoCases()
{
    return {
        /*
        测试思路：
        1. route param project_id 合法，且项目归当前用户所有。
        2. ProjectSvc::GetPatternInfoById 返回 pattern_info JSON 对象。
        3. handler 直接把 JSON 写入 data。

        示例：
          GET /projects/9501/pattern_info
              |
              v
          GetPatternInfoById -> {"length_policy":"no_length",...}
        */
        Case("Success",
            "查询 pattern_info 成功：返回 service 中保存的格式 JSON。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/" + std::to_string(kProjectId) + "/pattern_info",
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                EXPECT_CALL(*svc, GetPatternInfoById(_, kProjectId))
                    .WillOnce(Return(MinimalCustomTcpPatternInfo()));
                return svc;
            },
            InvokeQueryPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", 0}, {"message", "success"}, {"data", MinimalCustomTcpPatternInfo()}});
            }),

        /*
        测试思路：
        1. GetById 返回其他用户的 project。
        2. 当前用户不是管理员，鉴权失败。
        3. GetPatternInfoById 不应被调用。

        示例：
          owner=2,current_user=1 -> HTTP 403 forbidden
        */
        Case("ForbiddenWhenProjectBelongsToOtherUser",
            "普通用户查询别人的 pattern_info：返回 403。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/" + std::to_string(kProjectId) + "/pattern_info",
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId, kOtherUserId)));
                return svc;
            },
            InvokeQueryPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 鉴权通过。
        2. ProjectSvc::GetPatternInfoById 抛异常。
        3. handler 返回 service failed。

        示例：
          GetPatternInfoById throws -> {"code":-300,"message":"service failed"}
        */
        Case("ServiceQueryPatternInfoFailed",
            "查询 pattern_info 时 service 抛异常：返回 service failed。",
            [] {
                return MakeProjectRouteContext(
                    HttpRequest::Method::kGet,
                    "/projects/" + std::to_string(kProjectId) + "/pattern_info",
                    std::to_string(kProjectId));
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId)));
                EXPECT_CALL(*svc, GetPatternInfoById(_, kProjectId))
                    .WillOnce(Throw(std::runtime_error("pattern failed")));
                return svc;
            },
            InvokeQueryPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    nljson{{"code", -300}, {"message", "service failed"}});
            }),
    };
}

static std::vector<HandlerCase> MakeEditPatternInfoCases()
{
    return {
        /*
        测试思路：
        1. body 绑定 ProjectEditPatternInfoReq 成功。
        2. handler 只做用户鉴权，Custom TCP schema 校验和运行态停止校验交给 RuntimeController。
        3. RuntimeController::editPatternInfo 返回 persistedOk，表示 pattern_info 更新和协议项撤回成功。

        示例：
          POST /projects/pattern_info {id:9501, pattern_info:valid}
              |
              v
          GetById(owner=1) -> editPatternInfo -> persistedOk
        */
        Case("Success",
            "编辑 pattern_info 成功：鉴权通过，委托 RuntimeController 持久化更新并撤回协议项。",
            [] {
                return MakeEditPatternInfoContext(ProjectEditPatternInfoReq{
                    kProjectId,
                    MinimalCustomTcpPatternInfo(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(
                        kProjectId,
                        kCurrentUserId,
                        ProtocolType::kCustomTcp)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, editPatternInfo(_, kProjectId, Eq(MinimalCustomTcpPatternInfo())))
                    .WillOnce(Return(ProjectRuntimeResult::Success(
                        RuntimeMutationReceipt::PersistedOk(),
                        RuntimeSnapshot(kProjectId, ProjectRuntimeState::kStopped, 0))));
                return runtime;
            },
            InvokeEditPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(0, "success", 1, 0));
            }),

        /*
        测试思路：
        1. Content-Type 是 JSON，但 body 不是合法 JSON。
        2. handler 在鉴权前 Bind 失败。
        3. ProjectSvc 和 RuntimeController 都不应被调用。

        示例：
          broken JSON -> {"code":-200,"message":"body parse error"}
        */
        Case("RejectBrokenJsonBody",
            "编辑 pattern_info 时 body 解析失败：不进入鉴权和 service。",
            [] {
                auto ctx = MakeContext();
                SetRawBody(ctx->request(),
                    HttpRequest::Method::kPost,
                    "/projects/pattern_info",
                    ContentType(ContentType::kJsonType),
                    R"({"id":)");
                return ctx;
            },
            [] {
                return ExpectNoProjectSvcCalls();
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeEditPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "body parse error", 0, 0));
            }),

        /*
        测试思路：
        1. body 解析成功，但 GetById 返回其他用户的 project。
        2. 当前用户不是管理员，鉴权失败。
        3. 不调用 RuntimeController::editPatternInfo。

        示例：
          owner=2,current_user=1 -> HTTP 403 forbidden
        */
        Case("ForbiddenWhenProjectBelongsToOtherUser",
            "普通用户编辑别人的 pattern_info：返回 403。",
            [] {
                return MakeEditPatternInfoContext(ProjectEditPatternInfoReq{
                    kProjectId,
                    MinimalCustomTcpPatternInfo(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(kProjectId, kOtherUserId)));
                return svc;
            },
            [] {
                return ExpectNoRuntimeCalls();
            },
            InvokeEditPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k403Forbidden,
                    nljson{{"code", -403}, {"message", "forbidden"}, {"data", nljson::object()}});
            }),

        /*
        测试思路：
        1. 鉴权通过。
        2. pattern_info 为空对象，RuntimeController::editPatternInfo 校验 CustomTcpPatternSpec 失败。
        3. handler 只负责把 RuntimeController 的 kInvalidArgument 结果映射成写响应。

        示例：
          editPatternInfo({}) -> Failed(kInvalidArgument,"pattern info invalid")
              |
              v
          {"code":-200,"message":"pattern info invalid"}
        */
        Case("RejectInvalidPatternInfo",
            "编辑 pattern_info 时格式定义非法：RuntimeController 返回 pattern info invalid。",
            [] {
                return MakeEditPatternInfoContext(ProjectEditPatternInfoReq{
                    kProjectId,
                    nljson::object(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(
                        kProjectId,
                        kCurrentUserId,
                        ProtocolType::kCustomTcp)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, editPatternInfo(_, kProjectId, Eq(nljson::object())))
                    .WillOnce(Return(RuntimeFailure(RuntimeControlCode::kInvalidArgument, "pattern info invalid")));
                return runtime;
            },
            InvokeEditPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "pattern info invalid", 0, 0));
            }),

        /*
        测试思路：
        1. 鉴权通过，handler 把 pattern_info 编辑命令交给 RuntimeController。
        2. RuntimeController 发现 project 处于 running 或 registry 中有运行态 server。
        3. handler 映射 RuntimeController 的失败结果，提示用户先停止测试服务。

        示例：
          editPatternInfo(valid schema) -> Failed(kInvalidArgument,"请先停止测试服务后再修改格式信息")
              |
              v
          {"code":-200,"message":"请先停止测试服务后再修改格式信息"}
        */
        Case("RejectProjectRunning",
            "项目处于运行中时禁止编辑 pattern_info：RuntimeController 返回运行态拒绝。",
            [] {
                return MakeEditPatternInfoContext(ProjectEditPatternInfoReq{
                    kProjectId,
                    MinimalCustomTcpPatternInfo(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(
                        kProjectId,
                        kCurrentUserId,
                        ProtocolType::kCustomTcp,
                        ProjectStatus::kValid,
                        ProjectRuntimeState::kRunning)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, editPatternInfo(_, kProjectId, Eq(MinimalCustomTcpPatternInfo())))
                    .WillOnce(Return(RuntimeFailure(
                        RuntimeControlCode::kInvalidArgument,
                        "请先停止测试服务后再修改格式信息")));
                return runtime;
            },
            InvokeEditPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-200, "请先停止测试服务后再修改格式信息", 0, 0));
            }),

        /*
        测试思路：
        1. 鉴权通过，handler 调用 RuntimeController::editPatternInfo。
        2. RuntimeController 内部的 UpdatePatternInfoWithProtocolWithdraw 失败，并返回 kPersistFailed。
        3. handler 把 kPersistFailed 映射为 code=-300 的 service failed 响应。

        示例：
          editPatternInfo -> Failed(kPersistFailed,"service failed")
              |
              v
          {"code":-300,"message":"service failed"}
        */
        Case("ServiceUpdatePatternInfoFailed",
            "编辑 pattern_info 时 RuntimeController 持久化失败：返回 service failed。",
            [] {
                return MakeEditPatternInfoContext(ProjectEditPatternInfoReq{
                    kProjectId,
                    MinimalCustomTcpPatternInfo(),
                });
            },
            [] {
                auto svc = std::make_shared<StrictMock<MockProjectSvc>>();
                EXPECT_CALL(*svc, GetById(_, kProjectId))
                    .WillOnce(Return(MakeProject(
                        kProjectId,
                        kCurrentUserId,
                        ProtocolType::kCustomTcp)));
                return svc;
            },
            [] {
                auto runtime = std::make_shared<StrictMock<MockRuntimeController>>();
                EXPECT_CALL(*runtime, editPatternInfo(_, kProjectId, Eq(MinimalCustomTcpPatternInfo())))
                    .WillOnce(Return(RuntimeFailure(RuntimeControlCode::kPersistFailed, "service failed")));
                return runtime;
            },
            InvokeEditPatternInfo,
            [](HttpContextPtr ctx) {
                ExpectJsonResponse(ctx,
                    StateCode::k200Ok,
                    WriteBody(-300, "service failed", 0, 0));
            }),
    };
}

INSTANTIATE_TEST_SUITE_P(ProjectHandlerAddProject,
    ProjectHandlerParamTest,
    ValuesIn(MakeAddProjectCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerRuntimeState,
    ProjectHandlerParamTest,
    ValuesIn(MakeRuntimeStateCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerDelProject,
    ProjectHandlerParamTest,
    ValuesIn(MakeDelProjectCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerRestoreProject,
    ProjectHandlerParamTest,
    ValuesIn(MakeRestoreProjectCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerSingleProject,
    ProjectHandlerParamTest,
    ValuesIn(MakeSingleProjectCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerList,
    ProjectHandlerParamTest,
    ValuesIn(MakeListCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerGetAllValid,
    ProjectHandlerParamTest,
    ValuesIn(MakeGetAllValidCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerDetailName,
    ProjectHandlerParamTest,
    ValuesIn(MakeDetailNameCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerQueryPatternInfo,
    ProjectHandlerParamTest,
    ValuesIn(MakeQueryPatternInfoCases()),
    HandlerCaseName);

INSTANTIATE_TEST_SUITE_P(ProjectHandlerEditPatternInfo,
    ProjectHandlerParamTest,
    ValuesIn(MakeEditPatternInfoCases()),
    HandlerCaseName);

TEST(ProjectHandlerRegisterRoutesTest, RegistersAllProjectRoutes)
{
    // 测试思路：
    // 1. RegisterRoutes 只验证路由装配，不启动 HttpServer、不发真实 HTTP 请求。
    // 2. HttpServer 构造时使用 127.0.0.1:0 让系统分配端口，避免固定端口冲突。
    // 3. 断言 web_project.h 中声明的每个 Web handler 都有对应 pattern/method。
    //
    // 示例：
    //   ProjectHandler::RegisterRoutes(server)
    //          |
    //          v
    //   server.listRoutes() contains POST /projects/add, DELETE /projects/:project_id, ...
    KIT_LOGGER("net")->setLevel(LogLevel::ERROR);
    KIT_LOGGER("base")->setLevel(LogLevel::ERROR);
    KIT_LOGGER("web")->setLevel(LogLevel::ERROR);

    EventLoop loop;
    auto server = std::make_shared<kit_muduo::http::HttpServer>(
        &loop,
        InetAddress(0, "127.0.0.1"),
        "project-handler-routes-test",
        false,
        TcpServer::KReusePort);

    ProjectHandler handler(ExpectNoProjectSvcCalls(), ExpectNoProtocolSvcCalls(), ExpectNoRuntimeCalls());
    handler.RegisterRoutes(server);

    std::unordered_map<std::string, MethodMask> methods_by_pattern;
    for(const auto &route : server->listRoutes())
    {
        methods_by_pattern[route.pattern] |= route.methods;
    }

    const std::unordered_map<std::string, MethodMask> expected{
        {"/projects/add", ExpectHttpMethods::Post},
        {"/projects/:project_id/runtime_state", ExpectHttpMethods::Post},
        {"/projects/valid", ExpectHttpMethods::Get},
        {"/projects/:project_id", ExpectHttpMethods::Get | ExpectHttpMethods::Delete},
        {"/projects/:project_id/restore", ExpectHttpMethods::Post},
        {"/projects/:project_id/name", ExpectHttpMethods::Post},
        {"/projects/list", ExpectHttpMethods::Post},
        {"/projects/:project_id/pattern_info", ExpectHttpMethods::Get},
        {"/projects/pattern_info", ExpectHttpMethods::Post},
    };

    EXPECT_EQ(methods_by_pattern.size(), expected.size());
    for(const auto &item : expected)
    {
        const auto actual = methods_by_pattern.find(item.first);
        ASSERT_NE(actual, methods_by_pattern.end()) << item.first;
        EXPECT_EQ(actual->second, item.second) << item.first;
    }
}

} // namespace
