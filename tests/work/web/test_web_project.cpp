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
#include "domain/runtime_loop_pool.h"
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
#include "domain/user.h"
#include "service/mock/svc_project_mock.h"
#include "service/mock/svc_protocol_mock.h"
#include "../..//test_log.h"
#include "base/thread.h"
#include "base/event_loop_thread.h"
#include "net/event_loop.h"
#include "application.h"

#include <chrono>
#include <future>
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
    nljson                   pattern_info;      // 自定义 TCP 格式信息
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddProjectReq, name, mode, protocol_type, target_ip, pattern_info)
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

    void server_start()
    {
        loop_thread_ = std::make_unique<EventLoopThread>(nullptr, "test_server_loop");
        loop_ = loop_thread_->startLoop();
        ASSERT_NE(loop_, nullptr);

        InetAddress addr(SERVER_PORT, SERVER_IP);
        server_ = std::make_shared<kit_muduo::http::HttpServer>(
            loop_, addr, "test_server", false, TcpServer::Option::KReusePort);
        server_->setThreadNum(0);
        server_->setAuthCallback([](HttpContextPtr ctx) {
            SetCurrentUserToContext(ctx, CurrentUser{1, "web_project_tester", UserRole::kNormal, UserStatus::kActive});
            return kit_muduo::http::HttpServer::AuthCheckResult{};
        });

        handler_->RegisterRoutes(server_);
        server_->start();
        usleep(500000);
    }

    void server_stop()
    {
        if(server_ && loop_)
        {
            auto stopped = std::make_shared<std::promise<void>>();
            auto stopped_future = stopped->get_future();
            auto server = server_;
            loop_->runInLoop([server, stopped](){
                server->stopAsync([stopped](){
                    stopped->set_value();
                });
            });
            stopped_future.wait_for(std::chrono::seconds(2));

            auto released = std::make_shared<std::promise<void>>();
            auto released_future = released->get_future();
            loop_->runInLoop([this, released](){
                server_.reset();
                released->set_value();
            });
            released_future.wait_for(std::chrono::seconds(2));
        }
        else
        {
            server_.reset();
        }
        if(loop_thread_)
        {
            loop_thread_->quit();
        }
        loop_thread_.reset();
        loop_ = nullptr;
    }

    std::shared_ptr<ProjectSvcInterface> mock_svc_;
    std::unique_ptr<ProjectHandler> handler_;
    std::shared_ptr<HttpContext> ctx_;
    std::shared_ptr<kit_muduo::http::HttpServer> server_;
    std::unique_ptr<EventLoopThread> loop_thread_;
    EventLoop *loop_{nullptr};
    RuntimeLoopPool loop_pool_{1};
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
    SetCurrentUserToContext(ctx, CurrentUser{1, "web_project_tester", UserRole::kNormal, UserStatus::kActive});
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
        handler_ = std::make_unique<ProjectHandler>(mock_svc_, nullptr);
    
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

TEST_F(ProjectHandlerSuite, DelProject)
{
    constexpr int64_t project_id = 9501;
    constexpr int64_t service_fail_project_id = 9502;
    kit_app::Application app(nullptr);

    TestCases cases[] = {
        /*
        测试思路：
        1. 发送 DELETE /projects/{project_id}，Application 中没有运行态 server。
        2. handler 应跳过 runtime stop，只调用 service UpdateStatus(project_id, OFF_STATUS) 软删除项目。
        3. 断言响应 success，且不会额外创建运行态 server。

        示例：
          DELETE /projects/9501
          Application: {}
              |
              v
          UpdateStatus(9501, OFF_STATUS) -> true
        */
        {
            "1. 正常删除未运行项目",
            [](HttpRequestPtr req) -> void {
                ReqBuilderRawBodyHelper(req,
                    HttpRequest::Method::kDelete,
                    "/projects/" + std::to_string(project_id),
                    ContentType(),
                    "");
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                EXPECT_CALL(*mocksvc, GetById(_, project_id))
                    .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
                EXPECT_CALL(*mocksvc, UpdateStatus(_, project_id, ProjectStatus::OFF_STATUS))
                    .WillOnce(Return(true));
                return mocksvc;
            },
            200,
            R"({"code":0,"message":"success"})"
        },
        /*
        测试思路：
        1. 发送 DELETE /projects/not-a-number，路由参数存在但不能转成 int64_t。
        2. std::stol 抛出异常，handler 进入异常分支。
        3. 断言不会调用 service，响应 service failed，覆盖当前实现的异常处理行为。

        示例：
          DELETE /projects/not-a-number
              |
              v
          stol("not-a-number") throws -> {"code": -300, "message":"service failed"}
        */
        {
            "2. 路由参数不是数字",
            [](HttpRequestPtr req) -> void {
                ReqBuilderRawBodyHelper(req,
                    HttpRequest::Method::kDelete,
                    "/projects/not-a-number",
                    ContentType(),
                    "");
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                EXPECT_CALL(*mocksvc, UpdateStatus(_, _, _)).Times(0);
                return mocksvc;
            },
            200,
            R"({"code": -300, "message":"service failed"})"
        },
        /*
        测试思路：
        1. 发送合法 DELETE 请求，但 mock service 返回 false 模拟数据库状态更新失败。
        2. handler 将 UpdateStatus false 转换成异常分支。
        3. 断言响应 service failed。

        示例：
          DELETE /projects/9502
              |
              v
          UpdateStatus(9502, OFF_STATUS) -> false -> service failed
        */
        {
            "3. service update failed",
            [](HttpRequestPtr req) -> void {
                ReqBuilderRawBodyHelper(req,
                    HttpRequest::Method::kDelete,
                    "/projects/" + std::to_string(service_fail_project_id),
                    ContentType(),
                    "");
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                EXPECT_CALL(*mocksvc, GetById(_, service_fail_project_id))
                    .WillOnce(Return(MakeHttpProjectForStatus(service_fail_project_id)));
                EXPECT_CALL(*mocksvc, UpdateStatus(_, service_fail_project_id, ProjectStatus::OFF_STATUS))
                    .WillOnce(Return(false));
                return mocksvc;
            },
            200,
            R"({"code": -300, "message":"service failed"})"
        }
    };

    for(auto &c : cases)
    {
        CUSTOM_TEST_INFO_BEGIN(c.sub_name);
 
        ctx_ = std::make_shared<HttpContext>();
        auto req = ctx_->request();
        auto resp = ctx_->response();

        mock_svc_ = c.mock();
        handler_ = std::make_unique<ProjectHandler>(mock_svc_, nullptr);
        handler_->SetApp(&app);
    
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
1. 先构造并启动一个 HTTP runtime server，放入 Application 的 project server 表。
2. 通过真实 HTTP DELETE /projects/{project_id} 调用接口。
3. 断言 handler 在软删除前会 stop runtime server 并从 Application 移除。

示例：
  Application: 9503 -> running HttpProjectServer
        |
        | DELETE /projects/9503
        v
  runtime.stop() -> delServer(9503) -> UpdateStatus(9503, OFF_STATUS)
*/
TEST_F(ProjectHandlerSuite, DelProjectStopsRuntimeServerBeforeSoftDelete)
{
    constexpr int64_t project_id = 9503;
    kit_app::Application app(nullptr);

    auto lease_result = loop_pool_.acquire(project_id);
    ASSERT_TRUE(lease_result.ok()) << lease_result.error.toMsg();
    ASSERT_NE(lease_result.val, nullptr);

    auto runtime_server = std::make_shared<HttpProjectServer>(project_id, lease_result.val);
    runtime_server->start();
    ASSERT_TRUE(runtime_server->isActive());
    app.addServer(project_id, runtime_server);

    mock_svc_ = std::make_shared<MockProjectSvc>();
    auto mocksvc = std::dynamic_pointer_cast<MockProjectSvc>(mock_svc_);
    ASSERT_NE(mocksvc, nullptr);
    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mocksvc, UpdateStatus(_, project_id, ProjectStatus::OFF_STATUS))
        .WillOnce(Return(true));

    handler_ = std::make_unique<ProjectHandler>(mock_svc_, nullptr);
    handler_->SetApp(&app);

    ctx_ = std::make_shared<HttpContext>();
    auto req = ctx_->request();
    auto resp = ctx_->response();

    server_start();

    ReqBuilderRawBodyHelper(req,
        HttpRequest::Method::kDelete,
        "/projects/" + std::to_string(project_id),
        ContentType(),
        "");

    int res = tcp_send(req->toString(), ctx_);
    ASSERT_FALSE(res < 0);

    ASSERT_EQ(resp->stateCode()(), StateCode::k200Ok);
    ASSERT_STREQ(resp->body().toString().c_str(), R"({"code":0,"message":"success"})");
    EXPECT_EQ(app.findServer(project_id), nullptr);
    EXPECT_FALSE(runtime_server->isActive());

    server_stop();
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
  AddServer -> ProjectServer::start -> UpdateRuntimeStatus(true, listen_port)

举例：
  project_id=9401 开启成功后，response.data.listen_port 应大于 0；
  Application::FindServer(9401) 应能找到正在运行的 HttpProjectServer。
*/
TEST_F(ProjectHandlerSuite, StartProjectCreatesRuntimeStartsItAndReturnsListenPort)
{
    constexpr int64_t project_id = 9401;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto mock_protocol_svc = std::make_shared<NiceMock<MockProtocolSvc>>();
    auto handler = std::make_unique<ProjectHandler>(mocksvc, mock_protocol_svc);
    kit_app::Application app(nullptr);
    handler->SetApp(&app);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mock_protocol_svc, GetAllActive(_, project_id))
        .WillOnce(Return(std::vector<Protocol>{}));
    EXPECT_CALL(*mocksvc, UpdateRuntimeStatus(_, project_id, ProjectStatus::ON_STATUS, Gt(0)))
        .WillOnce(Return(true));

    auto ctx = MakeProjectStatusContext(project_id, ProjectStatus::ON_STATUS);

    handler->StartAndStopProject(nullptr, ctx);

    auto resp = ProjectResponseBody(ctx);
    ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
    EXPECT_EQ(resp["code"], 0);
    ASSERT_TRUE(resp["data"].contains("listen_port"));
    const uint16_t listen_port = resp["data"]["listen_port"].get<uint16_t>();
    EXPECT_GT(listen_port, 0);

    auto runtime_server = app.findServer(project_id);
    ASSERT_NE(runtime_server, nullptr);
    EXPECT_EQ(runtime_server->getProjectId(), project_id);
    EXPECT_TRUE(runtime_server->isActive());
    EXPECT_EQ(runtime_server->getBindAddr().toPort(), listen_port);

    runtime_server->stop();
    app.delServer(project_id);
}

/*
测试思路：
1. 预先构造并启动一个 HTTP runtime server，放入 Application server map。
2. 构造 /projects/{project_id}/status?operation=0 请求。
3. 调用 ProjectHandler::StartAndStopProject 后，断言 handler 会 stop runtime、从 map 删除 server，并写 DB active=false/listen_port=0。

示意：
  Application: project_id -> running server
        |
        | HTTP status=OFF
        v
  FindServer -> stop -> DelServer -> UpdateRuntimeStatus(false, 0)

举例：
  project_id=9402 停止成功后，Application::FindServer(9402) 应返回 nullptr；
  response.data.listen_port 按当前接口约定返回 0。
*/
TEST_F(ProjectHandlerSuite, StopProjectStopsRuntimeRemovesItAndReturnsSuccess)
{
    constexpr int64_t project_id = 9402;
    auto mocksvc = std::make_shared<NiceMock<MockProjectSvc>>();
    auto handler = std::make_unique<ProjectHandler>(mocksvc, nullptr);
    kit_app::Application app(nullptr);
    handler->SetApp(&app);

    auto result = loop_pool_.acquire(time(nullptr));
    ASSERT_EQ(result.ok(), true);
    ASSERT_NE(result.val, nullptr);

    auto runtime_server = std::make_shared<HttpProjectServer>(project_id, result.val);
    runtime_server->start();
    ASSERT_TRUE(runtime_server->isActive());
    app.addServer(project_id, runtime_server);

    EXPECT_CALL(*mocksvc, GetById(_, project_id))
        .WillOnce(Return(MakeHttpProjectForStatus(project_id)));
    EXPECT_CALL(*mocksvc, UpdateRuntimeStatus(_, project_id, ProjectStatus::OFF_STATUS, 0))
        .WillOnce(Return(true));

    auto ctx = MakeProjectStatusContext(project_id, ProjectStatus::OFF_STATUS);

    handler->StartAndStopProject(nullptr, ctx);

    auto resp = ProjectResponseBody(ctx);
    ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k200Ok);
    EXPECT_EQ(resp["code"], 0);
    EXPECT_EQ(resp["data"]["listen_port"], 0);
    EXPECT_EQ(app.findServer(project_id), nullptr);
    EXPECT_FALSE(runtime_server->isActive());
}
