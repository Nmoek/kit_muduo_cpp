#ifndef __KIT_DOMAIN_USER_H__
#define __KIT_DOMAIN_USER_H__

#include <cstdint>
#include <string>

#include "net/call_backs.h"
#include "nlohmann/json.hpp"

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
NLOHMANN_JSON_SERIALIZE_ENUM(UserStatus, {
    {UserStatus::kUnknown, "unknown"},
    {UserStatus::kActive, "active"},
    {UserStatus::kDisabled, "disabled"},
})
struct UserCandidate
{
    int64_t user_id;
    std::string note_name;
    UserStatus status;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserCandidate, user_id, note_name, status)
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
