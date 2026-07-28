#ifndef __KIT_DAO_USER_H__
#define __KIT_DAO_USER_H__

#include <cstdint>
#include <string>

namespace kit_dao {

struct User {
    int64_t m_id{0};
    std::string m_noteName;
    int32_t m_role{0};
    std::string m_passwordHash;
    int32_t m_status{0};
    int64_t m_ctime{0};
    int64_t m_utime{0};
};

struct UserCandidate
{
    int64_t id;
    std::string note_name;
    int32_t status;
};


} // namespace kit_dao

#endif // __KIT_DAO_USER_H__
