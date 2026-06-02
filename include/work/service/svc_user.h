#ifndef __KIT_SVC_USER_H__
#define __KIT_SVC_USER_H__

#include "domain/user.h"
#include "net/call_backs.h"

#include <memory>
#include <vector>

namespace kit_domain {

class UserRepoInterface;
class SessionRepoInterface;

struct UserCreateParam {
    std::string note_name;
    UserRole role{UserRole::kUnknown};
    std::string password;
};

struct UserUpdateParam {
    std::string note_name;
    UserRole role{UserRole::kUnknown};
    UserStatus status{UserStatus::kUnknown};
    std::string password;
};

struct UserListFilter {
    UserStatus status{UserStatus::kUnknown};
    int32_t offset{0};
    int32_t limit{20};
};

class UserService {
public:
    UserService(std::shared_ptr<UserRepoInterface> user_repo,
                std::shared_ptr<SessionRepoInterface> session_repo);

    int64_t AddUser(kit_muduo::HttpContextPtr ctx, const UserCreateParam &param);
    bool UpdateUser(kit_muduo::HttpContextPtr ctx, int64_t user_id, const UserUpdateParam &param);
    bool DisableUser(kit_muduo::HttpContextPtr ctx, int64_t user_id);
    bool RestoreUser(kit_muduo::HttpContextPtr ctx, int64_t user_id);
    User GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id);
    std::vector<User> List(kit_muduo::HttpContextPtr ctx, UserListFilter filter);

private:
    bool IsLastActiveAdmin(kit_muduo::HttpContextPtr ctx, const User &user);

private:
    std::shared_ptr<UserRepoInterface> user_repo_;
    std::shared_ptr<SessionRepoInterface> session_repo_;
};

} // namespace kit_domain

#endif // __KIT_SVC_USER_H__
