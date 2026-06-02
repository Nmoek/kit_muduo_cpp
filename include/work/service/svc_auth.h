/**
 * @file svc_auth.h
 * @brief 鉴权服务
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-02 00:48:20
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_SVC_AUTH_H__
#define __KIT_SVC_AUTH_H__

#include "domain/user.h"
#include "net/call_backs.h"

#include <memory>
#include <optional>
#include <string>

namespace kit_domain {

class UserRepoInterface;
class SessionRepoInterface;

struct LoginRequest {
    std::string note;
    std::string login_type;
    std::string password;
};

struct LoginResult {
    bool ok{false};
    CurrentUser user;
    std::string cookie_value;
    std::string message;
};

class AuthService {
public:
    AuthService(std::shared_ptr<UserRepoInterface> user_repo,
                std::shared_ptr<SessionRepoInterface> session_repo);

    LoginResult Login(kit_muduo::HttpContextPtr ctx, const LoginRequest &request);
    bool Logout(kit_muduo::HttpContextPtr ctx, const std::string &cookie_value);
    std::optional<CurrentUser> Authenticate(kit_muduo::HttpContextPtr ctx, const std::string &cookie_value);
    void BootstrapAdmin(kit_muduo::HttpContextPtr ctx = nullptr);

private:
    std::shared_ptr<UserRepoInterface> user_repo_;
    std::shared_ptr<SessionRepoInterface> session_repo_;
};

std::string ExtractCookieValue(const std::string &cookie_header, const std::string &cookie_name);

} // namespace kit_domain

#endif // __KIT_SVC_AUTH_H__
