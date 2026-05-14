/**
 * @file test_web_project.cpp
 * @brief web接口单元测试
 * @author ljk5
 * @version 1.0
 * @date 2025-07-18 19:24:30
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "work/web/web_project.h"
#include "net/http/http_server.h"
#include "net/tcp_connection.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_context.h"
#include "domain/project.h"
#include "domain/project_server.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "domain/http_protocol_item.h"
#include "domain/runtime_result.h"
#include "service/mock/svc_project_mock.h"
#include "../..//test_log.h"
#include "base/thread.h"
#include "base/event_loop_thread.h"
#include "net/event_loop.h"
#include "application.h"

#include <memory>
#include <string>
#include <vector>


using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;
using namespace testing;

#define CUSTOM_TEST_INFO_BEGIN(sub_name) do{ \
    SCOPED_TRACE("Testing: [" + sub_name + "]"); \
    std::cout << "[ RUN      ] " << "[" << sub_name << "]" << std::endl;\
}while (0)



#define CUSTOM_TEST_INFO_END(sub_name) do{\
    std::cout << "[       OK ] " << "[" << sub_name << "]" << std::endl;\
}while(0)



const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8888;

struct AddProjectReq {
    std::string              name;              // 测试名称
    int32_t                  mode;              // 测试模式
    int32_t                  protocol_type;     // 协议种类
    std::string              target_ip;         // 目标ip + 端口 x.x.x.x:8888
    int32_t                  pattern_type;      // 自定义 TCP 格式类型
    nljson                   pattern_info;      // 自定义 TCP 格式信息
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddProjectReq, name, mode, protocol_type, target_ip, pattern_type, pattern_info)
};

struct DelProjectReq {
    int64_t m_id;   // 测试服务Id
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DelProjectReq, m_id)
};

struct OthreadReq {
    std::string              m_name;
    int32_t                  m_mode;
    std::string              str1;
    int32_t                  num2;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(OthreadReq, m_name, m_mode, str1, num2)
};

using ReqBuildFunc = std::function<void(HttpRequestPtr)>;
using MockSvcFunc = std::function<std::shared_ptr<ProjectSvcInterface>()>;

struct TestCases {
    std::string sub_name;       //子项名称
    ReqBuildFunc reqBuild;      //请求参数
    MockSvcFunc  mock;         // service 接口

    int32_t wantCode;          // 期望状态码
    std::string wantBody;      // 期望响应Body
};

static nljson HttpReqCfg(const std::string &method,
                         const std::string &path,
                         const nljson &headers)
{
    return nljson{
        {"method", method},
        {"path", path},
        {"headers", headers},
    };
}

static nljson HttpRespCfg(const std::string &status_code,
                          const nljson &headers)
{
    return nljson{
        {"status_code", status_code},
        {"headers", headers},
    };
}

static std::shared_ptr<Protocol> MakeHttpProtocol(
        int64_t protocol_id,
        int64_t project_id,
        const std::string &path,
        const std::vector<char> &req_body = {},
        const std::vector<char> &resp_body = {'o', 'k'})
{
    auto protocol = std::make_shared<Protocol>();
    protocol->m_id = protocol_id;
    protocol->m_name = "http_runtime_pc_" + std::to_string(protocol_id);
    protocol->m_type = ProtocolType::HTTP_PROTOCOL;
    protocol->m_projectId = project_id;
    protocol->m_status = ProtocolStatus::ACTIVE;
    protocol->m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE;
    protocol->m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE;
    protocol->m_reqBodyDataStatus = req_body.empty() ? 0 : 1;
    protocol->m_respBodyDataStatus = resp_body.empty() ? 0 : 1;
    protocol->m_reqCfg = HttpReqCfg("GET", path, nljson{{"X-Old", "1"}});
    protocol->m_respCfg = HttpRespCfg("200", nljson{{"Content-Type", "application/json"}});
    protocol->m_reqBodyData = req_body;
    protocol->m_respBodyData = resp_body;
    protocol->m_isEndian = true;
    protocol->m_ctime = TimeStamp::Now();
    protocol->m_utime = TimeStamp::Now();
    return protocol;
}

static std::shared_ptr<HttpProtocolItem> GetHttpRuntimeItem(
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

class ProjectHandlerSuite : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto l = KIT_LOGGER("net");
        auto l2 = KIT_LOGGER("base");
        auto l3 = KIT_LOGGER("web");
        l->setLevel(LogLevel::ERROR);
        l2->setLevel(LogLevel::ERROR);
        l3->setLevel(LogLevel::ERROR);

    }

    void TearDown() override
    {
        server_stop();
    }

    void server_start()
    {
        loop_thread_ = std::make_unique<EventLoopThread>(nullptr, "test_server_loop");
        loop_ = loop_thread_->startLoop();
        ASSERT_NE(loop_, nullptr);

        InetAddress addr(SERVER_PORT, SERVER_IP);
        server_ = std::make_shared<kit_muduo::http::HttpServer>(
            loop_, addr, "test_server", false, TcpServer::Option::KReusePort);
        server_->setThreadNum(0);

        handler_->RegisterRoutes(server_);
        server_->start();
        usleep(500000);
    }

    void server_stop()
    {
        if(loop_thread_)
        {
            loop_thread_->quit();
        }
        server_.reset();
        loop_thread_.reset();
        loop_ = nullptr;
    }

    std::shared_ptr<ProjectSvcInterface> mock_svc_;
    ProjectHandler* handler_;
    std::shared_ptr<HttpContext> ctx_;
    std::shared_ptr<kit_muduo::http::HttpServer> server_;
    std::unique_ptr<EventLoopThread> loop_thread_;
    EventLoop *loop_{nullptr};
};


static int tcp_send(const std::string &input, HttpContextPtr ctx) 
{
    int client_fd;
    struct sockaddr_in server_addr;
    auto resp = ctx->response();

    // 1. 创建socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        TEST_ERROR() << "socket creation failed " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }
    
    // 2. 配置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // 将IP地址从字符串转换为网络地址
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) 
    {
        TEST_ERROR() << "invalid address/address not supported " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }
    
    // 3. 连接到服务器
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        TEST_ERROR() << "connection failed " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }


    // 4. 发送数据
    send(client_fd, input.data(), input.size(), 0);

    TEST_INFO() << std::endl << input << std::endl;


    // 5. 接收响应
    Buffer buf;
    int32_t savedErrno = 0;
    int valread = buf.readFd(client_fd, &savedErrno);
    if(valread <= 0)
    {
        TEST_ERROR() << "read failed " 
        << savedErrno << ":" 
        << strerror(savedErrno) << std::endl;
        close(client_fd);

        return valread;
    }
    
    (void)ctx->parseResponse(buf, TimeStamp());

    // 6. 关闭连接
    close(client_fd);
    
    return valread;
}

template<typename T>
static void ReqBuilderHelper(HttpRequestPtr req, int32_t method, const std::string &path, T data) noexcept
{
    std::string host = SERVER_IP;
    host += ":";
    host += std::to_string(SERVER_PORT);

    // 设置请求参数
    req->setVersion(Version::kHttp11);
    req->setMethod(method); 
    req->setPath(path);  // ==> AddProject
    req->addHeader("Content-Type", "application/json");
    req->addHeader("User-Agent", "PostmanRuntime/7.44.1");
    req->addHeader("Host", host);
    req->addHeader("Connection", "keep-alive");

    Body body((ContentType(ContentType::kJsonType)));
    nljson root = data;
    body.appendData(root.dump());
    
    req->setBody(body);
}

static void ReqBuilderRawBodyHelper(HttpRequestPtr req,
                                    int32_t method,
                                    const std::string &path,
                                    ContentType content_type,
                                    const std::string &body_data) noexcept
{
    std::string host = SERVER_IP;
    host += ":";
    host += std::to_string(SERVER_PORT);

    req->setVersion(Version::kHttp11);
    req->setMethod(method);
    req->setPath(path);
    req->addHeader("User-Agent", "PostmanRuntime/7.44.1");
    req->addHeader("Host", host);
    req->addHeader("Connection", "keep-alive");

    Body body(content_type);
    body.appendData(body_data);
    req->setBody(body);
}

static Project MakeHttpProjectForStatus(int64_t project_id)
{
    Project p;
    p.m_id = project_id;
    p.m_name = "status_http_project_" + std::to_string(project_id);
    p.m_mode = ProjectMode::ServerMode;
    p.m_protocolType = ProtocolType::HTTP_PROTOCOL;
    p.m_listenPort = 0;
    p.m_targetIp = "";
    p.m_userId = 1;
    p.m_status = ProjectStatus::ON_STATUS;
    p.m_active = ProjectStatus::OFF_STATUS;
    p.m_patternType = CustomTcpPatternType::STANDARD;
    p.m_patternInfo = nljson::object();
    p.m_ctime = TimeStamp::Now();
    return p;
}

static HttpContextPtr MakeProjectStatusContext(int64_t project_id, ProjectStatus operation)
{
    auto ctx = std::make_shared<HttpContext>();
    auto req = ctx->request();
    req->setVersion(Version::kHttp11);
    req->setMethod(HttpRequest::Method::kPost);
    req->setPath("/projects/" + std::to_string(project_id) + "/status");
    req->addRouteParam("project_id", std::to_string(project_id));
    req->addQureyParam("operation", std::to_string(static_cast<int32_t>(operation)));
    req->addHeader("Content-Type", "application/json");
    return ctx;
}

static nljson ProjectResponseBody(HttpContextPtr ctx)
{
    return nljson::parse(ctx->response()->body().toString());
}

/**
 * @brief ProjectHandler.AddProject 接口单元测试
 */
TEST_F(ProjectHandlerSuite, AddProject) 
{
    TestCases cases[] =  {
        /*
        测试思路：
        1. 发送符合当前 AddProjectReq JSON schema 的新增项目请求。
        2. mock service 返回 project_id=1。
        3. 断言 handler 返回 success，并把 project_id 写入 data。

        示例报文：
          POST /projects/add
          {"name":"test1","mode":1,"protocol_type":1,...}
        */
        {
            "1. 正常返回",
            [](HttpRequestPtr req) -> void {
                ReqBuilderHelper(req, 
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    AddProjectReq{
                        "test1",
                        ProjectMode::ServerMode,
                        static_cast<int32_t>(ProtocolType::HTTP_PROTOCOL),
                        "",
                        static_cast<int32_t>(CustomTcpPatternType::STANDARD),
                        nljson::object()
                    }
                );
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                EXPECT_CALL(*mocksvc, Add(_, _))
                    .WillOnce(Return(1));
                return mocksvc;
            },
            200,
            R"({"code":0,"data":{"project_id":1},"message":"success"})"
        },
        /*
        测试思路：
        1. 请求 body 使用 XML 格式，同时 Content-Type 也声明为 application/xml。
        2. 当前 AddProject 只支持 JSON 绑定，ctx->Bind 会返回 false。
        3. 断言不会进入 service Add，handler 返回 body parse error。

        示例报文：
          Content-Type: application/xml
          <project><name>test1</name></project>
        */
        {
            "2. Body不是json格式",
            [](HttpRequestPtr req) {
                ReqBuilderRawBodyHelper(req,
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    ContentType(ContentType::kXmlType),
                    "<project><name>test1</name></project>");
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                return mocksvc;
            },
            200,
            R"({"code": -200, "message":"body parse error"})"
        },
        /*
        测试思路：
        1. 请求体合法，service Add 返回 -1 模拟持久化失败。
        2. AddProject 将 service 失败统一转换为业务错误。
        3. 断言响应为 service failed。

        示例：
          mock Add(...) -> -1
          response.code -> -300
        */
        {
            "3. service add failed",
            [](HttpRequestPtr req) {
                ReqBuilderHelper(req, 
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    AddProjectReq{
                        "test1",
                        ProjectMode::ServerMode,
                        static_cast<int32_t>(ProtocolType::HTTP_PROTOCOL),
                        "",
                        static_cast<int32_t>(CustomTcpPatternType::STANDARD),
                        nljson::object()
                    }
                ); 
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                EXPECT_CALL(*mocksvc, Add(_, _)).WillOnce(Return(-1));
                return mocksvc;
            },
            200,
            R"({"code": -300, "message":"service failed"})"
        },
        /*
        测试思路：
        1. Content-Type 声明为 application/json，但 body 内容不是合法 JSON。
        2. nlohmann::json::parse 抛出异常，HttpContext::Bind 捕获后返回 false。
        3. 断言 handler 返回 body parse error，不触发 service Add。

        示例报文：
          Content-Type: application/json
          {"name":
        */
        {
            "4. 请求Body异常",
            [](HttpRequestPtr req) {
                ReqBuilderRawBodyHelper(req,
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    ContentType(ContentType::kJsonType),
                    R"({"name":)");
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                return mocksvc;
            }
            ,200
            ,R"({"code": -200, "message":"body parse error"})"
        }
    };

    for(auto &c : cases)
    {
        CUSTOM_TEST_INFO_BEGIN(c.sub_name);
 
        ctx_ = std::make_shared<HttpContext>();
        auto req = ctx_->request();
        auto resp = ctx_->response();
    
        mock_svc_ = c.mock();
        handler_ = ProjectHandler::Instance(mock_svc_);
    
        // 开启服务器
        server_start();
        
        // 构造请求
        c.reqBuild(req);

        // 执行测试 socket直接发送
        int res = tcp_send(req->toString(), ctx_);
        ASSERT_FALSE(res < 0);
    
        // 验证结果 5. 变化
        ASSERT_EQ(resp->stateCode()(), StateCode::k200Ok);
        // 验证body 6.变化
        if(c.wantBody.size())
        {
            ASSERT_STREQ(resp->body().toString().c_str(), c.wantBody.c_str());
        }

        CUSTOM_TEST_INFO_END(c.sub_name);

        server_stop();
    }
}

TEST_F(ProjectHandlerSuite, DISABLED_DelProject)
{
    TestCases cases[] = {
        {}
    };

    for(auto &c : cases)
    {
        CUSTOM_TEST_INFO_BEGIN(c.sub_name);
 
        ctx_ = std::make_shared<HttpContext>();
        auto req = ctx_->request();
        auto resp = ctx_->response();

        mock_svc_ = c.mock();
        handler_ = ProjectHandler::Instance(mock_svc_);
    
        // 开启服务器
        server_start();
        
        // 构造请求
        c.reqBuild(req);

        // 执行测试 socket直接发送
        int res = tcp_send(req->toString(), ctx_);
        ASSERT_FALSE(res < 0);
    
        // 验证结果 5. 变化
        ASSERT_EQ(resp->stateCode()(), StateCode::k200Ok);
        // 验证body 6.变化
        if(c.wantBody.size())
        {
            ASSERT_STREQ(resp->body().toString().c_str(), c.wantBody.c_str());
        }

        CUSTOM_TEST_INFO_END(c.sub_name);

        server_stop();
    }
}

/*
测试思路：
1. 构造 /projects/{project_id}/status?operation=1 请求，service 返回一个未运行的 HTTP project。
2. 直接调用 ProjectHandler::StartAndStopProject。
3. 断言 handler 会创建 runtime server、写入 Application server map、调用 start()，并把实际监听端口返回给前端。

示意：
  HTTP status=ON
        |
        v
  GetById -> ProjectServerFactory::Create(只创建)
        |
        v
  AddServer -> ProjectServer::start -> UpdateActiveStatus(true)

举例：
  project_id=9401 开启成功后，response.data.listen_port 应大于 0；
  Application::FindServer(9401) 应能找到正在运行的 HttpProjectServer。
*/
TEST_F(ProjectHandlerSuite, StartProjectCreatesRuntimeStartsItAndReturnsListenPort)
{
    constexpr int64_t project_id = 9401;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto handler = ProjectHandler::Instance(mocksvc);
    kit_app::Application app(nullptr);
    handler->SetApp(&app);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mocksvc, UpdateActiveStatus(_, project_id, true))
        .WillOnce(Return(true));

    auto ctx = MakeProjectStatusContext(project_id, ProjectStatus::ON_STATUS);

    handler->StartAndStopProject(nullptr, ctx);

    auto resp = ProjectResponseBody(ctx);
    ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
    EXPECT_EQ(resp["code"], 0);
    ASSERT_TRUE(resp["data"].contains("listen_port"));
    const uint16_t listen_port = resp["data"]["listen_port"].get<uint16_t>();
    EXPECT_GT(listen_port, 0);

    auto runtime_server = app.FindServer(project_id);
    ASSERT_NE(runtime_server, nullptr);
    EXPECT_EQ(runtime_server->getProjectId(), project_id);
    EXPECT_TRUE(runtime_server->isActive());
    EXPECT_EQ(runtime_server->getBindAddr().toPort(), listen_port);

    runtime_server->stop();
    app.DelServer(project_id);
}

/*
测试思路：
1. 预先构造并启动一个 HTTP runtime server，放入 Application server map。
2. 构造 /projects/{project_id}/status?operation=0 请求。
3. 调用 ProjectHandler::StartAndStopProject 后，断言 handler 会 stop runtime、从 map 删除 server，并写 DB active=false。

示意：
  Application: project_id -> running server
        |
        | HTTP status=OFF
        v
  FindServer -> stop -> DelServer -> UpdateActiveStatus(false)

举例：
  project_id=9402 停止成功后，Application::FindServer(9402) 应返回 nullptr；
  response.data.listen_port 按当前接口约定返回 0。
*/
TEST_F(ProjectHandlerSuite, StopProjectStopsRuntimeRemovesItAndReturnsSuccess)
{
    constexpr int64_t project_id = 9402;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto handler = ProjectHandler::Instance(mocksvc);
    kit_app::Application app(nullptr);
    handler->SetApp(&app);

    auto runtime_server = std::make_shared<HttpProjectServer>(project_id);
    runtime_server->start();
    ASSERT_TRUE(runtime_server->isActive());
    app.AddServer(project_id, runtime_server);

    EXPECT_CALL(*mocksvc, GetById(_, _)).Times(0);
    EXPECT_CALL(*mocksvc, UpdateActiveStatus(_, project_id, false))
        .WillOnce(Return(true));

    auto ctx = MakeProjectStatusContext(project_id, ProjectStatus::OFF_STATUS);

    handler->StartAndStopProject(nullptr, ctx);

    auto resp = ProjectResponseBody(ctx);
    ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
    EXPECT_EQ(resp["code"], 0);
    EXPECT_EQ(resp["data"]["listen_port"], 0);
    EXPECT_EQ(app.FindServer(project_id), nullptr);
    EXPECT_FALSE(runtime_server->isActive());
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，初始路由为 GET /d9/http/header。
2. 只更新 req headers，method/path 保持不变。
3. 更新后断言 req cfg 中 header 已替换，但 req/resp body view 的共享指针没有变化。

示意：
  [route: GET /d9/http/header] + [req body A] + [resp body B]
                    |
                    | UpdateReqCfg(headers)
                    v
  [route: GET /d9/http/header] + [req body A] + [resp body B]

举例：
  旧 headers: {"X-Old":"1"}
  新 headers: {"X-New":"2"}
*/
TEST(HttpProjectRuntimeSuite, UpdateReqHeadersKeepsRouteAndBodyViews)
{
    auto server = std::make_shared<HttpProjectServer>(9001);
    auto protocol = MakeHttpProtocol(101, 9001, "/d9/http/header", {'r', 'e', 'q'}, {'r', 'e', 's', 'p'});
    auto item = ProtocolItemFactory::Create(protocol, server);
    ASSERT_NE(item, nullptr);

    auto add_result = server->AddProtocolItem(item);
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto before = GetHttpRuntimeItem(server, 101);
    ASSERT_NE(before, nullptr);
    const auto before_req_body = before->getReqBodyView();
    const auto before_resp_body = before->getRespBodyView();

    auto update_result = server->UpdateReqCfgProtocolItem(
        101,
        HttpReqCfg("GET", "/d9/http/header", nljson{{"X-New", "2"}}));

    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();
    auto after = GetHttpRuntimeItem(server, 101);
    ASSERT_NE(after, nullptr);

    const auto req_cfg = after->getReqCfg();
    EXPECT_EQ(req_cfg.method.toInt(), HttpRequest::Method::kGet);
    EXPECT_EQ(req_cfg.path, "/d9/http/header");
    ASSERT_EQ(req_cfg.headers.count("X-New"), 1u);
    EXPECT_EQ(req_cfg.headers.at("X-New"), "2");
    EXPECT_EQ(req_cfg.headers.count("X-Old"), 0u);
    EXPECT_EQ(before_req_body.body_data, after->getReqBodyView().body_data);
    EXPECT_EQ(before_resp_body.body_data, after->getRespBodyView().body_data);
}

/*
测试思路：
1. 构造两个 HTTP 运行态协议项：
   - 协议 201: GET /d9/http/old
   - 协议 202: GET /d9/http/conflict
2. 把协议 201 的 path 更新成 /d9/http/conflict，触发新 route 冲突。
3. 断言更新失败后，协议 201 仍保持旧 path，协议 202 仍保持自己的 path。

示意：
  pc201 -> GET /old       pc202 -> GET /conflict
      \       Update pc201 to /conflict
       \______________X route conflict

举例：
  尝试让两个 GET 共用同一个 exact path，HttpServletDispatch 应返回 route conflict。
*/
TEST(HttpProjectRuntimeSuite, UpdateReqRouteConflictPreservesOldRouteAndCfg)
{
    auto server = std::make_shared<HttpProjectServer>(9002);
    auto old_protocol = MakeHttpProtocol(201, 9002, "/d9/http/old");
    auto conflict_protocol = MakeHttpProtocol(202, 9002, "/d9/http/conflict");

    auto add_old_result = server->AddProtocolItem(ProtocolItemFactory::Create(old_protocol, server));
    ASSERT_TRUE(add_old_result.ok()) << add_old_result.error.toMsg();
    auto add_conflict_result = server->AddProtocolItem(ProtocolItemFactory::Create(conflict_protocol, server));
    ASSERT_TRUE(add_conflict_result.ok()) << add_conflict_result.error.toMsg();

    auto update_result = server->UpdateReqCfgProtocolItem(
        201,
        HttpReqCfg("GET", "/d9/http/conflict", nljson{{"X-Try", "conflict"}}));

    ASSERT_FALSE(update_result.ok());
    EXPECT_EQ(update_result.error.toInt(), RuntimeError::kRouteConflict);

    auto old_item = GetHttpRuntimeItem(server, 201);
    ASSERT_NE(old_item, nullptr);
    EXPECT_EQ(old_item->getReqCfg().path, "/d9/http/old");
    EXPECT_EQ(old_item->getReqCfg().headers.count("X-Try"), 0u);

    auto conflict_item = GetHttpRuntimeItem(server, 202);
    ASSERT_NE(conflict_item, nullptr);
    EXPECT_EQ(conflict_item->getReqCfg().path, "/d9/http/conflict");
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，初始路由为 GET /d9/http/move-old。
2. 更新 method/path 到 POST /d9/http/move-new。
3. 再新增一个使用旧路由 GET /d9/http/move-old 的协议项。
4. 如果新增成功，说明旧 route 已释放；同时当前协议项持有的新 method/path 已提交。

示意：
  pc301: GET /move-old
      |
      | UpdateReqCfg(POST /move-new)
      v
  pc301: POST /move-new     pc302: GET /move-old 可重新注册

举例：
  route 切换成功后，旧 exact route 不应继续占用路由表。
*/
TEST(HttpProjectRuntimeSuite, UpdateReqRouteSuccessReleasesOldRoute)
{
    auto server = std::make_shared<HttpProjectServer>(9003);
    auto protocol = MakeHttpProtocol(301, 9003, "/d9/http/move-old");

    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto update_result = server->UpdateReqCfgProtocolItem(
        301,
        HttpReqCfg("POST", "/d9/http/move-new", nljson{{"X-New", "route"}}));
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    auto moved_item = GetHttpRuntimeItem(server, 301);
    ASSERT_NE(moved_item, nullptr);
    EXPECT_EQ(moved_item->getReqCfg().method.toInt(), HttpRequest::Method::kPost);
    EXPECT_EQ(moved_item->getReqCfg().path, "/d9/http/move-new");

    auto old_route_protocol = MakeHttpProtocol(302, 9003, "/d9/http/move-old");
    auto add_old_route_result = server->AddProtocolItem(ProtocolItemFactory::Create(old_route_protocol, server));
    ASSERT_TRUE(add_old_route_result.ok()) << add_old_route_result.error.toMsg();

    auto old_route_item = GetHttpRuntimeItem(server, 302);
    ASSERT_NE(old_route_item, nullptr);
    EXPECT_EQ(old_route_item->getReqCfg().path, "/d9/http/move-old");
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，记录 req cfg、resp cfg 和 req body view 指针。
2. 只调用 UpdateReqBodyProtocolItem 替换请求 body。
3. 断言 req/resp cfg 不变，req body view 指针发生替换且数据变成新值。

示意：
  [req cfg H] + [resp cfg R] + [req body old]
                         |
                         | UpdateReqBody(new)
                         v
  [req cfg H] + [resp cfg R] + [req body new]

举例：
  更新大 body 时不应该重新解析或复制 HTTP header/status 配置块。
*/
TEST(HttpProjectRuntimeSuite, UpdateReqBodyOnlyReplacesReqBodyView)
{
    auto server = std::make_shared<HttpProjectServer>(9004);
    auto protocol = MakeHttpProtocol(401, 9004, "/d9/http/body", {'o', 'l', 'd'}, {'r', 'e', 's', 'p'});

    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto before = GetHttpRuntimeItem(server, 401);
    ASSERT_NE(before, nullptr);
    const auto before_req_cfg = before->getReqCfg();
    const auto before_resp_cfg = before->getRespCfg();
    const auto before_req_body = before->getReqBodyView();
    const auto before_resp_body = before->getRespBodyView();

    const std::vector<char> new_body{'n', 'e', 'w', '-', 'b', 'o', 'd', 'y'};
    auto update_result = server->UpdateReqBodyProtocolItem(
        401,
        ProtocolBodyType::JSON_BODY_TYPE,
        new_body);

    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();
    auto after = GetHttpRuntimeItem(server, 401);
    ASSERT_NE(after, nullptr);

    EXPECT_EQ(after->getReqCfg().method.toInt(), before_req_cfg.method.toInt());
    EXPECT_EQ(after->getReqCfg().path, before_req_cfg.path);
    EXPECT_EQ(after->getReqCfg().headers, before_req_cfg.headers);
    EXPECT_EQ(after->getRespCfg().state_code.toInt(), before_resp_cfg.state_code.toInt());
    EXPECT_EQ(after->getRespCfg().headers, before_resp_cfg.headers);
    EXPECT_NE(after->getReqBodyView().body_data, before_req_body.body_data);
    EXPECT_EQ(*after->getReqBodyView().body_data, new_body);
    EXPECT_EQ(after->getRespBodyView().body_data, before_resp_body.body_data);
}
