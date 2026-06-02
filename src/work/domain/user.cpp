#include "domain/user.h"

#include "net/http/http_context.h"

#include <cctype>

namespace kit_domain {

std::string UserRoleToString(UserRole role)
{
    switch(role)
    {
        case UserRole::kNormal: return "normal";
        case UserRole::kAdmin: return "admin";
        default: return "unknown";
    }
}

UserRole UserRoleFromString(const std::string &role)
{
    if(role == "normal") return UserRole::kNormal;
    if(role == "admin") return UserRole::kAdmin;
    return UserRole::kUnknown;
}

std::string UserStatusToString(UserStatus status)
{
    switch(status)
    {
        case UserStatus::kActive: return "active";
        case UserStatus::kDisabled: return "disabled";
        default: return "unknown";
    }
}

UserStatus UserStatusFromString(const std::string &status)
{
    if(status == "active") return UserStatus::kActive;
    if(status == "disabled") return UserStatus::kDisabled;
    return UserStatus::kUnknown;
}

bool IsValidNoteName(const std::string &note_name)
{
    if(note_name.size() < 3 || note_name.size() > 32)
    {
        return false;
    }
    for(unsigned char c : note_name)
    {
        if(!std::isalnum(c))
        {
            return false;
        }
    }
    return true;
}

void SetCurrentUserToContext(kit_muduo::HttpContextPtr ctx, const CurrentUser &user)
{
    if(!ctx)
    {
        return;
    }
    ctx->setAttribute("auth.user_id", std::to_string(user.user_id));
    ctx->setAttribute("auth.note_name", user.note_name);
    ctx->setAttribute("auth.role", UserRoleToString(user.role));
    ctx->setAttribute("auth.status", UserStatusToString(user.status));
}

CurrentUser CurrentUserFromContext(kit_muduo::HttpContextPtr ctx)
{
    CurrentUser user;
    if(!ctx)
    {
        return user;
    }
    const std::string user_id = ctx->attribute("auth.user_id");
    if(!user_id.empty())
    {
        user.user_id = std::stoll(user_id);
    }
    user.note_name = ctx->attribute("auth.note_name");
    user.role = UserRoleFromString(ctx->attribute("auth.role"));
    user.status = UserStatusFromString(ctx->attribute("auth.status"));
    return user;
}

} // namespace kit_domain
