/**
 * @file web_project.h
 * @brief  测试服务 web层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 15:06:44
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_WEB_PROJECT_H__
#define __KIT_WEB_PROJECT_H__

#include <memory>

#include "net/call_backs.h"

namespace kit_muduo {
namespace http {
    class HttpServer;
}
}

namespace kit_app {
class Application;
}

namespace kit_domain
{
class ProjectSvcInterface;
class ProtocolSvcInterface;

class ProjectHandler
{
public:
    ProjectHandler(std::shared_ptr<ProjectSvcInterface> svc, std::shared_ptr<ProtocolSvcInterface> pc_svc);
    ~ProjectHandler();

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

    void SetApp(kit_app::Application *app) { _app = app; }
    kit_app::Application* GetApp() const { return _app; }

public:
    void AddProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void StartAndStopProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void DelProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void RestoreProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void SingleProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    
    void List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void GetAllValid(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;


    void QueryPatternInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;


    void EditPatternInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

private:
    std::shared_ptr<ProjectSvcInterface> _svc;
    std::shared_ptr<ProtocolSvcInterface> _pc_svc;
    kit_app::Application *_app;
    
};

} // namespace kit_domain
#endif //__KIT_WEB_PROJECT_H__
