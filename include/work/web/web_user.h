#ifndef __KIT_WEB_USER_H__
#define __KIT_WEB_USER_H__

#include "net/call_backs.h"

#include <memory>

namespace kit_muduo::http {
class HttpServer;
}

namespace kit_domain {

class UserService;

class UserHandler {
public:
    explicit UserHandler(std::shared_ptr<UserService> user_svc);
    ~UserHandler() = default;

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

    void List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Add(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Get(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Update(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Disable(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;
    void Restore(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept;

private:
    std::shared_ptr<UserService> user_svc_;
};

} // namespace kit_domain

#endif // __KIT_WEB_USER_H__
