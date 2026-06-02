#include "repository/repo_user.h"

#include "dao/dao_user.h"

namespace kit_domain {

namespace {
kit_dao::User ToDaoUser(const User &user)
{
    return kit_dao::User{
        user.id,
        user.note_name,
        static_cast<int32_t>(user.role),
        user.password_hash,
        static_cast<int32_t>(user.status),
        user.ctime,
        user.utime,
    };
}

User ToDomainUser(const kit_dao::User &user)
{
    return User{
        user.m_id,
        user.m_noteName,
        static_cast<UserRole>(user.m_role),
        user.m_passwordHash,
        static_cast<UserStatus>(user.m_status),
        user.m_ctime,
        user.m_utime,
    };
}
}

UserRepository::UserRepository(std::shared_ptr<kit_dao::UserDaoInterface> dao)
    :UserRepoInterface(std::move(dao))
{
}

int64_t UserRepository::Create(kit_muduo::HttpContextPtr ctx, const User &user)
{
    return _dao->Insert(ctx, ToDaoUser(user));
}

bool UserRepository::Update(kit_muduo::HttpContextPtr ctx, const User &user)
{
    return _dao->Update(ctx, ToDaoUser(user));
}

bool UserRepository::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t user_id, UserStatus status)
{
    return _dao->UpdateStatus(ctx, user_id, static_cast<int32_t>(status));
}

bool UserRepository::UpdatePasswordHash(kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash)
{
    return _dao->UpdatePasswordHash(ctx, user_id, password_hash);
}

User UserRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    return ToDomainUser(_dao->GetById(ctx, user_id));
}

User UserRepository::GetByNoteName(kit_muduo::HttpContextPtr ctx, const std::string &note_name)
{
    return ToDomainUser(_dao->GetByNoteName(ctx, note_name));
}

std::vector<User> UserRepository::List(kit_muduo::HttpContextPtr ctx, UserStatus status, int32_t offset, int32_t limit)
{
    std::vector<User> users;
    auto dao_users = _dao->List(ctx, static_cast<int32_t>(status), offset, limit);
    for(const auto &dao_user : dao_users)
    {
        users.emplace_back(ToDomainUser(dao_user));
    }
    return users;
}

int32_t UserRepository::CountActiveAdmin(kit_muduo::HttpContextPtr ctx)
{
    return _dao->CountActiveAdmin(ctx);
}

} // namespace kit_domain
