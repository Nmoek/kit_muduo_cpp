#ifndef __KIT_DAO_SESSION_H__
#define __KIT_DAO_SESSION_H__

#include <cstdint>
#include <string>

namespace kit_dao {

struct UserSession {
    int64_t m_id{0};
    int64_t m_userId{0};
    std::string m_secretHash;
    int64_t m_expireTime{0};
    int64_t m_ctime{0};
    int64_t m_utime{0};
};

} // namespace kit_dao

#endif // __KIT_DAO_SESSION_H__
