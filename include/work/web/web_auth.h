#ifndef __KIT_WEB_AUTH_H__
#define __KIT_WEB_AUTH_H__

#include "net/call_backs.h"

#include <memory>

namespace kit_muduo::http {
class HttpServer;
}

namespace kit_domain {

class AuthService;

class AuthHandler {
public:
    explicit AuthHandler(std::shared_ptr<AuthService> auth_svc);
    ~AuthHandler() = default;

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

    void Login(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Logout(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Me(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

private:
    std::shared_ptr<AuthService> auth_svc_;
};

} // namespace kit_domain

#endif // __KIT_WEB_AUTH_H__
