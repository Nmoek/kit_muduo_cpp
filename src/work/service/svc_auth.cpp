/**
 * @file svc_auth.cpp
 * @brief 鉴权服务
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-02 00:47:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "service/svc_auth.h"
#include "service/svc_log.h"
#include "base/time_stamp.h"
#include "base/util.h"
#include "dao/session.h"
#include "repository/repo_session.h"
#include "repository/repo_user.h"
#include "service/password_hasher.h"

#include <cstdlib>
#include <stdexcept>

namespace kit_domain {

namespace {

// 默认会话超时时间12h
constexpr int64_t kSessionTtlMs = 12LL * 60 * 60 * 1000;

bool ParseSessionCookie(const std::string &cookie_value, int64_t &session_id, std::string &secret)
{
    const auto pos = cookie_value.find('.');
    if(pos == std::string::npos || pos == 0 || pos + 1 >= cookie_value.size())
    {
        return false;
    }
    try {
        session_id = std::stoll(cookie_value.substr(0, pos));
    } catch(const std::exception &) {
        return false;
    }
    secret = cookie_value.substr(pos + 1);
    return session_id > 0 && !secret.empty();
}

CurrentUser ToCurrentUser(const User &user)
{
    return CurrentUser{
        user.id,
        user.note_name,
        user.role,
        user.status,
    };
}
}


AuthService::AuthService(std::shared_ptr<UserRepoInterface> user_repo,
                         std::shared_ptr<SessionRepoInterface> session_repo)
    :user_repo_(std::move(user_repo))
    ,session_repo_(std::move(session_repo))
{
}

LoginResult AuthService::Login(kit_muduo::HttpContextPtr ctx, const LoginRequest &request)
{
    LoginResult result;
    if(!IsValidNoteName(request.note))
    {
        result.message = "invalid note";
        return result;
    }

    const User user = user_repo_->GetByNoteName(ctx, request.note);
    if(user.id <= 0 || user.status != UserStatus::kActive)
    {
        result.message = "invalid user";
        return result;
    }

    if(request.login_type == "normal")
    {
        if(user.role != UserRole::kNormal)
        {
            result.message = "invalid login type";
            return result;
        }
    }
    else if(request.login_type == "admin")
    {
        if(user.role != UserRole::kAdmin || !PasswordHasher::Verify(request.password, user.password_hash))
        {
            result.message = "invalid password";
            return result;
        }

        if(PasswordHasher::NeedsRehash(user.password_hash))
        {
            const std::string upgraded_hash = PasswordHasher::Hash(request.password);
            if(!user_repo_->UpdatePasswordHash(ctx, user.id, upgraded_hash))
            {
                SVCAUTH_F_ERROR("password hash upgrade failed, user_id[%ld]\n", user.id);
            }
        }
    }
    else
    {
        result.message = "invalid login type";
        return result;
    }

    const std::string random_secret = kit_muduo::GenerateUuid() + kit_muduo::GenerateUuid();
    kit_dao::UserSession session;
    session.m_userId = user.id;
    session.m_secretHash = PasswordHasher::Hash(random_secret);
    session.m_expireTime = kit_muduo::TimeStamp::NowMs() + kSessionTtlMs;

    const int64_t session_id = session_repo_->Create(ctx, session);
    if(session_id <= 0)
    {
        result.message = "session create failed";
        return result;
    }

    result.ok = true;
    result.user = ToCurrentUser(user);
    result.cookie_value = std::to_string(session_id) + "." + random_secret;
    result.message = "success";
    return result;
}

bool AuthService::Logout(kit_muduo::HttpContextPtr ctx, const std::string &cookie_value)
{
    int64_t session_id = 0;
    std::string secret;
    if(!ParseSessionCookie(cookie_value, session_id, secret))
    {
        return false;
    }
    return session_repo_->DeleteById(ctx, session_id);
}

std::optional<CurrentUser> AuthService::Authenticate(kit_muduo::HttpContextPtr ctx, const std::string &cookie_value)
{
    session_repo_->DeleteExpired(ctx, kit_muduo::TimeStamp::NowMs());

    int64_t session_id = 0;
    std::string secret;
    if(!ParseSessionCookie(cookie_value, session_id, secret))
    {
        return std::nullopt;
    }

    const auto session = session_repo_->GetById(ctx, session_id);
    if(session.m_id <= 0 || session.m_expireTime <= kit_muduo::TimeStamp::NowMs())
    {
        return std::nullopt;
    }
    if(!PasswordHasher::Verify(secret, session.m_secretHash))
    {
        return std::nullopt;
    }

    const User user = user_repo_->GetById(ctx, session.m_userId);
    if(user.id <= 0 || user.status != UserStatus::kActive)
    {
        return std::nullopt;
    }

    CurrentUser current_user = ToCurrentUser(user);
    SetCurrentUserToContext(ctx, current_user);
    return current_user;
}

void AuthService::BootstrapAdmin(kit_muduo::HttpContextPtr ctx)
{
    const int32_t active_admin_count = user_repo_->CountActiveAdmin(ctx);
    if(active_admin_count < 0)
    {
        throw std::runtime_error("count active admin failed");
    }
    if(active_admin_count > 0)
    {
        return;
    }

    const char *note_env = std::getenv("KIT_ADMIN_NOTE");
    const char *password_env = std::getenv("KIT_ADMIN_PASSWORD");
    const std::string note = note_env ? note_env : "";
    const std::string password = password_env ? password_env : "";
    if(!IsValidNoteName(note) || password.empty())
    {
        throw std::runtime_error("KIT_ADMIN_NOTE or KIT_ADMIN_PASSWORD invalid");
    }


    User user;
    user.note_name = note;
    user.role = UserRole::kAdmin;
    user.password_hash = PasswordHasher::Hash(password);
    user.status = UserStatus::kActive;
    if(user_repo_->Create(ctx, user) <= 0)
    {
        throw std::runtime_error("bootstrap admin create failed");
    }
}

std::string ExtractCookieValue(const std::string &cookie_header, const std::string &cookie_name)
{
    size_t start = 0;
    while(start < cookie_header.size())
    {
        while(start < cookie_header.size() && (cookie_header[start] == ' ' || cookie_header[start] == ';'))
        {
            ++start;
        }
        const size_t end = cookie_header.find(';', start);
        const size_t token_end = end == std::string::npos ? cookie_header.size() : end;
        const std::string token = cookie_header.substr(start, token_end - start);
        const size_t eq = token.find('=');
        if(eq != std::string::npos && token.substr(0, eq) == cookie_name)
        {
            return token.substr(eq + 1);
        }
        if(end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return "";
}

} // namespace kit_domain
