/**
 * @file main.cpp
 * @brief 主程序
 * @author ljk5
 * @version 1.0
 * @date 2025-07-19 02:34:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "application.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/protocol_interaction_publisher.h"
#include "net/http/http_server.h"
#include "net/event_loop.h"
#include "web/web_project.h"
#include "service/svc_project.h"
#include "repository/repo_project.h"
#include "dao/dao_project.h"

#include "web/web_auth.h"
#include "web/web_protocol_interaction.h"
#include "web/web_user.h"
#include "service/svc_auth.h"
#include "service/svc_user.h"
#include "repository/repo_session.h"
#include "repository/repo_user.h"
#include "dao/dao_session.h"
#include "dao/dao_user.h"

#include "web/web_protocol.h"
#include "service/svc_protocol.h"
#include "repository/repo_protocol.h"
#include "dao/dao_protocol.h"
#include "dao/sqlite_orm_pool.h"

#include "runtime/runtime_controller.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_server.h"
#include "service/svc_auth.h"
#include "domain/protocol_interaction_publisher.h"

#include "dao/init.h"
#include "ioc/web.h"

#include <functional>
#include <memory>
#include <vector>
#include <signal.h>
#if defined(__SANITIZE_ADDRESS__) || defined(__has_feature)
 #if defined(__has_feature)
  #if __has_feature(address_sanitizer)
   #define ASAN_DEBUG 1
  #endif
 #else
  #define ASAN_DEBUG 1
 #endif
#endif
#if ASAN_DEBUG
#include <sanitizer/lsan_interface.h>
#endif


using namespace kit_app;
using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;
using namespace kit_dao;

/// @brief 全局事件循环
static EventLoop loop;

static void InitLog(void)
{
    auto l = KIT_LOGGER("base");
    auto l2 = KIT_LOGGER("net");
    auto l3 = KIT_LOGGER("web");
    l->addAppender(std::make_shared<FileAppender>("log/base.log"));
    l2->addAppender(std::make_shared<FileAppender>("log/net.log"));
    l3->addAppender(std::make_shared<FileAppender>("log/web.log"));
    // l->setLevel(LogLevel::INFO);
    // l2->setLevel(LogLevel::INFO);
    // l3->setLevel(LogLevel::INFO);

}

/**
 * @brief 该函数就是所有依赖初始化的地方
 * @return std::shared_ptr<Application> 
 */
static std::shared_ptr<Application> InitApp()
{
    // TODO 根据配置文件进行数据库初始化
    auto sqliteDbPool = InitSqliteDbPool(SqliteOrmPoolConfig());

    std::shared_ptr<ProtocolDaoInterface> protocDao = std::make_shared<SqliteOrmProtocolDao>(sqliteDbPool);
    std::shared_ptr<ProtocolRepoInterface> protocRepo = std::make_shared<ProtocolRepository>(protocDao);
    std::shared_ptr<ProtocolSvcInterface> protocSvc = std::make_shared<ProtocolService>(protocRepo);

    std::shared_ptr<ProjectDaoInterface> projDao = std::make_shared<SqliteOrmProjectDao>(sqliteDbPool);
    std::shared_ptr<ProjectRepoInterface> projRepo = std::make_shared<ProjectRepository>(projDao);
    std::shared_ptr<ProjectSvcInterface> projSvc = std::make_shared<ProjectService>(projRepo);

    std::shared_ptr<UserDaoInterface> userDao = std::make_shared<SqliteOrmUserDao>(sqliteDbPool);
    std::shared_ptr<UserRepoInterface> userRepo = std::make_shared<UserRepository>(userDao);
    std::shared_ptr<SessionDaoInterface> sessionDao = std::make_shared<SqliteOrmSessionDao>(sqliteDbPool);
    std::shared_ptr<SessionRepoInterface> sessionRepo = std::make_shared<SessionRepository>(sessionDao);
    auto authSvc = std::make_shared<AuthService>(userRepo, sessionRepo);
    auto userSvc = std::make_shared<UserService>(userRepo, sessionRepo);
    authSvc->BootstrapAdmin();


    // 全局协议交互详情订阅器
    std::shared_ptr<ProtocolInteractionHub> hub = std::make_shared<ProtocolInteractionHub>();

    // 全局协议交互详情发布器
    auto publisher = std::make_shared<ProtocolInteractionPublisher>(
        std::vector<std::shared_ptr<InteractionSink>>{hub}
    );
    publisher->start();
    
    // 全局运行态管理器
    std::shared_ptr<RuntimeControllerInterface> runtime_controller = std::make_shared<ProjectRuntimeManager>(projSvc, protocSvc, publisher);
    

    // 需要将app句柄放到Handler中
    static std::shared_ptr<ProtocolHandler> protocHdl;
    static std::shared_ptr<ProjectHandler> projHdl;
    static std::shared_ptr<AuthHandler> authHdl;
    static std::shared_ptr<UserHandler> userHdl;
    static std::shared_ptr<ProtocolInteractionHandler> interHdl;

    protocHdl = std::make_shared<ProtocolHandler>(protocSvc, projSvc, runtime_controller);
    projHdl = std::make_shared<ProjectHandler>(projSvc, protocSvc,runtime_controller);
    authHdl = std::make_shared<AuthHandler>(authSvc);
    userHdl = std::make_shared<UserHandler>(userSvc);
    interHdl = std::make_shared<ProtocolInteractionHandler>(protocSvc, runtime_controller, hub);

    //需要给运行态增加live清理回调
    runtime_controller->setInteractionCleanUpCallBack(std::bind(&ProtocolInteractionHandler::cleanupLive, interHdl, std::placeholders::_1, std::placeholders::_2));

    auto server = InitWebServer(&loop, projHdl.get(), protocHdl.get(), authHdl.get(), userHdl.get(), interHdl.get());

    server->setAuthCallback([authSvc](HttpContextPtr ctx) {
        const std::string path = ctx->request()->path();

        const std::vector<std::string> allow_paths_no_auth{
            "/html/login.html",
            "/css/login.css",
            "/js/namespace.js",
            "/js/config.js",
            "/js/utils.js",
            "/js/mock_data.js",
            "/js/api.js",
            "/js/auth.js",
            "/js/login.js",
            "/auth/login",
        };

        bool is_login_asset = false;
        
        for(auto &allow_path : allow_paths_no_auth)
        {
            if(allow_path == path)
            {
                is_login_asset = true;
                break;
            }
        }

        if(is_login_asset
            || path.find("/assets/") == 0)
        {
            return HttpServer::AuthCheckResult{};
        }

        const std::string cookie = ExtractCookieValue(ctx->request()->getHeader("Cookie"), "kit_session");
        auto current_user = authSvc->Authenticate(ctx, cookie);
        if(!current_user.has_value())
        {
            return HttpServer::AuthCheckResult{
                false,
                StateCode::k401Unauthorized,
                R"({"code":-401,"message":"unauthorized","data":{}})",
                path.find("/html/") == 0
            };
        }

        const bool is_admin_path = path.find("/users/") == 0
            || path == "/users/list"
            || path == "/users/add";
        if(is_admin_path && !current_user->IsAdmin())
        {
            return HttpServer::AuthCheckResult{
                false,
                StateCode::k403Forbidden,
                R"({"code":-403,"message":"forbidden","data":{}})",
                false
            };
        }

        return HttpServer::AuthCheckResult{};
    });

    auto app = std::make_shared<Application>(server, runtime_controller);


    // 先恢复当前库上正在运行的服务器, 恢复服务器的同时需要重新添加协议
    if(!app->recover())
    {
        std::cerr << "application recover error!" << std::endl;
    }


    return app;
}

void setup_asan_report() 
{
#if ASAN_DEBUG
    // 注册信号处理函数
    signal(SIGUSR1, [](int sig) {

        std::cerr << "触发内存泄漏检查..." << std::endl;

        __lsan_do_recoverable_leak_check();  // 主动执行泄漏检查
    });
#endif
}

int main(int argc, char* argv[])
{
    std::shared_ptr<Application> app = nullptr;
    std::shared_ptr<kit_muduo::http::HttpServer> server = nullptr;


    // ASan触发检查
    setup_asan_report();


    try {
        InitLog();
        app = InitApp();
    } catch(std::exception &e) {
        std::cerr << "application init fail!" << e.what() << std::endl;
        abort();
    }


    server = app->server();
    server->start();
    loop.loop();
    return 0;
}
