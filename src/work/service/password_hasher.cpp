#include "service/password_hasher.h"

#include <crypt.h>
#include <stdexcept>

namespace kit_domain {

std::string PasswordHasher::Hash(const std::string &password)
{
    char salt[CRYPT_GENSALT_OUTPUT_SIZE] = {0};
    if(!crypt_gensalt_rn("$y$", 5, nullptr, 0, salt, sizeof(salt)))
    {
        throw std::runtime_error("crypt_gensalt failed");
    }

    crypt_data data{};
    char *result = crypt_r(password.c_str(), salt, &data);
    if(!result || result[0] == '*')
    {
        throw std::runtime_error("crypt_r failed");
    }

    return result;
}

bool PasswordHasher::Verify(const std::string &password, const std::string &hash)
{
    if(hash.empty())
    {
        return false;
    }

    crypt_data data{};
    char *result = crypt_r(password.c_str(), hash.c_str(), &data);
    return result && hash == result;
}

} // namespace kit_domain
