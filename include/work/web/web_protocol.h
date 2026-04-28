/**
 * @file web_protocol.h.h
 * @brief 测试协议项 web层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-25 18:03:15
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_WEB_PROTOCOL_H__
#define __KIT_WEB_PROTOCOL_H__

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
class ProtocolSvcInterface;

class ProtocolHandler
{
public:
    ~ProtocolHandler();

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

    
    void SetApp(kit_app::Application *app) { _app = app; }
    kit_app::Application* GetApp() const { return _app; }

public:
    /**
     * @brief 单例模式
     * @param svc 
     * @return ProtocolHandler* 
     */
    static ProtocolHandler* Instance(std::shared_ptr<ProtocolSvcInterface> svc);

public:
    void AddProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void DelProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void SingleProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    
    void List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void DetailCfg(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void DetailBody(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;


    void GetCfg(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void QueryCommonFields(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void ProtocolCnt(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void GetProtocolBodyType(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

    void GetProtocolBodyData(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    
    void GetProtocolBodyInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    

private:
    ProtocolHandler(std::shared_ptr<ProtocolSvcInterface> svc);

private:
    std::shared_ptr<ProtocolSvcInterface> _svc;
    kit_app::Application *_app;
};

} // namespace kit_domain
#endif //__KIT_WEB_PROJECT_H__