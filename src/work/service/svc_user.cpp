#include "service/svc_user.h"

#include "repository/repo_session.h"
#include "repository/repo_user.h"
#include "service/password_hasher.h"

#include <stdexcept>

namespace kit_domain {

namespace {
bool IsValidUserRole(UserRole role)
{
    return role == UserRole::kNormal || role == UserRole::kAdmin;
}

bool IsValidUserStatus(UserStatus status)
{
    return status == UserStatus::kActive || status == UserStatus::kDisabled;
}
}

UserService::UserService(std::shared_ptr<UserRepoInterface> user_repo,
                         std::shared_ptr<SessionRepoInterface> session_repo)
    :user_repo_(std::move(user_repo))
    ,session_repo_(std::move(session_repo))
{
}

int64_t UserService::AddUser(kit_muduo::HttpContextPtr ctx, const UserCreateParam &param)
{
    if(!IsValidNoteName(param.note_name) || !IsValidUserRole(param.role))
    {
        return -1;
    }
    if(param.role == UserRole::kAdmin && param.password.empty())
    {
        return -1;
    }

    User user;
    user.note_name = param.note_name;
    user.role = param.role;
    user.password_hash = param.role == UserRole::kAdmin ? PasswordHasher::Hash(param.password) : "";
    user.status = UserStatus::kActive;
    return user_repo_->Create(ctx, user);
}

bool UserService::UpdateUser(kit_muduo::HttpContextPtr ctx, int64_t user_id, const UserUpdateParam &param)
{
    User user = user_repo_->GetById(ctx, user_id);
    if(user.id <= 0)
    {
        return false;
    }
    if(!IsValidNoteName(param.note_name) || !IsValidUserRole(param.role) || !IsValidUserStatus(param.status))
    {
        return false;
    }
    if(param.role == UserRole::kNormal && IsLastActiveAdmin(ctx, user))
    {
        return false;
    }
    if(param.status == UserStatus::kDisabled && IsLastActiveAdmin(ctx, user))
    {
        return false;
    }

    user.note_name = param.note_name;
    user.role = param.role;
    user.status = param.status;
    if(param.role == UserRole::kAdmin && !param.password.empty())
    {
        user.password_hash = PasswordHasher::Hash(param.password);
    }
    if(param.role == UserRole::kNormal)
    {
        user.password_hash.clear();
    }

    const bool ok = user_repo_->Update(ctx, user);
    if(ok && user.status == UserStatus::kDisabled)
    {
        session_repo_->DeleteByUserId(ctx, user.id);
    }
    return ok;
}

bool UserService::DisableUser(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    User user = user_repo_->GetById(ctx, user_id);
    if(user.id <= 0 || IsLastActiveAdmin(ctx, user))
    {
        return false;
    }
    const bool ok = user_repo_->UpdateStatus(ctx, user_id, UserStatus::kDisabled);
    if(ok)
    {
        session_repo_->DeleteByUserId(ctx, user_id);
    }
    return ok;
}

bool UserService::RestoreUser(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    return user_repo_->UpdateStatus(ctx, user_id, UserStatus::kActive);
}

User UserService::GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    return user_repo_->GetById(ctx, user_id);
}

std::vector<User> UserService::List(kit_muduo::HttpContextPtr ctx, UserListFilter filter)
{
    if(filter.limit <= 0 || filter.limit > 200)
    {
        filter.limit = 20;
    }
    if(filter.offset < 0)
    {
        filter.offset = 0;
    }
    return user_repo_->List(ctx, filter.status, filter.offset, filter.limit);
}

bool UserService::IsLastActiveAdmin(kit_muduo::HttpContextPtr ctx, const User &user)
{
    return user.role == UserRole::kAdmin
        && user.status == UserStatus::kActive
        && user_repo_->CountActiveAdmin(ctx) <= 1;
}

} // namespace kit_domain
