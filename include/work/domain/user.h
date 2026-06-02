#ifndef __KIT_DOMAIN_USER_H__
#define __KIT_DOMAIN_USER_H__

#include <cstdint>
#include <string>

#include "net/call_backs.h"

namespace kit_domain {

enum class UserRole {
    kUnknown = 0,
    kNormal = 1,
    kAdmin = 2,
};

enum class UserStatus {
    kUnknown = 0,
    kActive = 1,
    kDisabled = 2,
};

struct CurrentUser {
    int64_t user_id{0};
    std::string note_name;
    UserRole role{UserRole::kUnknown};
    UserStatus status{UserStatus::kUnknown};

    bool IsAdmin() const { return role == UserRole::kAdmin; }
    bool IsNormal() const { return role == UserRole::kNormal; }
};

struct User {
    int64_t id{0};
    std::string note_name;
    UserRole role{UserRole::kUnknown};
    std::string password_hash;
    UserStatus status{UserStatus::kUnknown};
    int64_t ctime{0};
    int64_t utime{0};
};

std::string UserRoleToString(UserRole role);
UserRole UserRoleFromString(const std::string &role);
std::string UserStatusToString(UserStatus status);
UserStatus UserStatusFromString(const std::string &status);

bool IsValidNoteName(const std::string &note_name);
void SetCurrentUserToContext(kit_muduo::HttpContextPtr ctx, const CurrentUser &user);
CurrentUser CurrentUserFromContext(kit_muduo::HttpContextPtr ctx);

} // namespace kit_domain

#endif // __KIT_DOMAIN_USER_H__
