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
#include "service/mock/svc_project_mock.h"
#include "../..//test_log.h"
#include "base/thread.h"
#include "net/event_loop.h"
#include "application.h"


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



const char* SERVER_IP = "10.6.170.65";
const int SERVER_PORT = 8888;

struct AddProjectReq {
    std::string              m_name;            // 测试名称
    int32_t                  m_mode;            // 测试模式
    uint16_t                 m_listenPort;      // 监听端口号
    std::string              m_targetIp;        // 目标ip + 端口 x.x.x.x:8888
    int64_t                  m_userId;          // 所属用户id
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddProjectReq, m_name, m_mode, m_listenPort, m_targetIp, m_userId)
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
        t_ = std::make_shared<Thread>([this](){
            InetAddress addr(SERVER_PORT, SERVER_IP);
            auto server = std::make_shared<HttpServer>(&loop_, addr, "test_server", false, TcpServer::Option::KReusePort);
            server->setThreadNum(1);
            
            handler_->RegisterRoutes(server);
    
            server->start();
            loop_.loop();
            
        }, "test_server");
        t_->start();
        usleep(500000);
    }

    std::shared_ptr<ProjectSvcInterface> mock_svc_;
    ProjectHandler* handler_;
    std::shared_ptr<HttpContext> ctx_;
    std::shared_ptr<Thread> t_;
    EventLoop loop_;
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

/**
 * @brief ProjectHandler.AddProject 接口单元测试
 */
TEST_F(ProjectHandlerSuite, AddProject) 
{
    TestCases cases[] =  {
        {
            "1. 正常返回",
            [](HttpRequestPtr req) -> void {
                ReqBuilderHelper(req, 
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    AddProjectReq{
                        "test1", 1, 8080, "", 1
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
            R"({"code": 0, "message":"success"})"
        },
        {
            "2. Body不是json格式",
            [](HttpRequestPtr req) {
                ReqBuilderHelper(req, 
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    AddProjectReq{
                        "test1", 1, 8080, "", 1
                    }
                ); 
                req->addHeader("Content-Type", "application/xml");
            },
            []() -> std::shared_ptr<ProjectSvcInterface> {
                auto mocksvc = std::make_shared<MockProjectSvc>();
                return mocksvc;
            },
            200,
            R"({"code": -100, "message":"content-type is not json"})"
        },
        {
            "3. service add failed",
            [](HttpRequestPtr req) {
                ReqBuilderHelper(req, 
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    AddProjectReq{
                        "test1", 1, 8080, "", 1
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
        {
            "4. 请求Body异常",
            [](HttpRequestPtr req) {
                ReqBuilderHelper(req, 
                    HttpRequest::Method::kPost,
                    "/projects/add",
                    OthreadReq{
                        "test1", 1, "xxxx", 100
                    }
                ); 
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

        loop_.quit();
        t_->join();
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

        loop_.quit();
        t_->join();
    }
}