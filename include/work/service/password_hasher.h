#ifndef __KIT_PASSWORD_HASHER_H__
#define __KIT_PASSWORD_HASHER_H__

#include <string>

namespace kit_domain {

class PasswordHasher {
public:
    static std::string Hash(const std::string &password);
    static bool Verify(const std::string &password, const std::string &hash);
};

} // namespace kit_domain

#endif // __KIT_PASSWORD_HASHER_H__
