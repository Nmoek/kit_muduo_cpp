/**
 * @file main.cpp
 * @brief 主程序
 * @author ljk5
 * @version 1.0
 * @date 2025-07-19 02:34:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "application.h"
#include "net/http/http_server.h"
#include "net/event_loop.h"
#include "web/web_project.h"
#include "service/svc_project.h"
#include "repository/repo_project.h"
#include "dao/dao_project.h"

#include "web/web_auth.h"
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

#include "dao/init.h"
#include "ioc/web.h"

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
    l->setLevel(LogLevel::INFO);
    l2->setLevel(LogLevel::INFO);
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

    // 需要将app句柄放到Handler中
    static std::shared_ptr<ProtocolHandler> protocHdl;
    static std::shared_ptr<ProjectHandler> projHdl;
    static std::shared_ptr<AuthHandler> authHdl;
    static std::shared_ptr<UserHandler> userHdl;
    protocHdl = std::make_shared<ProtocolHandler>(protocSvc);
    projHdl = std::make_shared<ProjectHandler>(projSvc, protocSvc);
    authHdl = std::make_shared<AuthHandler>(authSvc);
    userHdl = std::make_shared<UserHandler>(userSvc);
    auto server = InitWebServer(&loop, projHdl.get(), protocHdl.get(), authHdl.get(), userHdl.get(), authSvc);

    auto app = std::make_shared<Application>(server);
    
    projHdl->SetApp(app.get()); // 避免循环依赖 app生命周期更长
    protocHdl->SetApp(app.get()); // 避免循环依赖 app生命周期更长
    protocHdl->SetProjectService(projSvc);


    // 先恢复当前库上正在运行的服务器, 恢复服务器的同时需要重新添加协议
    if(!app->recover(projSvc, protocSvc))
    {
        std::cerr << "application recover error!" << std::endl;
        abort();
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
