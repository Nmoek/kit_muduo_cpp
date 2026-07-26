#include "service/password_hasher.h"
#include "base/util.h"

#include <crypt.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>



namespace kit_domain {

namespace {

constexpr const char *kCurrentHashPrefix = "$scrypt$";
constexpr const char *kLegacyHashPrefix = "$y$";
constexpr std::uint64_t kScryptN = 1ULL << 15;
constexpr std::uint64_t kScryptR = 8;
constexpr std::uint64_t kScryptP = 1;
constexpr std::uint64_t kScryptMaxMemory = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kDigestSize = 32;
constexpr std::size_t kLegacySha256HashSize = kSaltSize + kDigestSize;

struct ParsedCurrentHash {
    std::uint64_t n = 0;
    std::uint64_t r = 0;
    std::uint64_t p = 0;
    std::array<unsigned char, kSaltSize> salt{};
    std::array<unsigned char, kDigestSize> digest{};
};

bool HasPrefix(const std::string &value, const char *prefix)
{
    return value.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

std::string EncodeHex(const unsigned char *data, std::size_t size)
{
    constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for(std::size_t i = 0; i < size; ++i)
    {
        result.push_back(kHex[data[i] >> 4]);
        result.push_back(kHex[data[i] & 0x0f]);
    }
    return result;
}

int HexValue(char value)
{
    if(value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if(value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if(value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}

bool DecodeHex(std::string_view encoded, unsigned char *output, std::size_t output_size)
{
    if(encoded.size() != output_size * 2)
    {
        return false;
    }
    for(std::size_t i = 0; i < output_size; ++i)
    {
        const int high = HexValue(encoded[i * 2]);
        const int low = HexValue(encoded[i * 2 + 1]);
        if(high < 0 || low < 0)
        {
            return false;
        }
        output[i] = static_cast<unsigned char>((high << 4) | low);
    }
    return true;
}


bool ParseCurrentHash(const std::string &hash, ParsedCurrentHash &parsed)
{
    if(!HasPrefix(hash, kCurrentHashPrefix))
    {
        return false;
    }

    std::string_view fields = std::string_view(hash).substr(
        std::char_traits<char>::length(kCurrentHashPrefix));
    std::array<std::string_view, 5> values{};
    for(std::size_t i = 0; i < values.size(); ++i)
    {
        const std::size_t separator = fields.find('$');
        if(i + 1 == values.size())
        {
            if(separator != std::string_view::npos)
            {
                return false;
            }
            values[i] = fields;
        }
        else
        {
            if(separator == std::string_view::npos)
            {
                return false;
            }
            values[i] = fields.substr(0, separator);
            fields.remove_prefix(separator + 1);
        }
    }

    if(!kit_muduo::ParsePositiveArithmetic(std::string(values[0]), parsed.n)
        || !kit_muduo::ParsePositiveArithmetic(std::string(values[1]), parsed.r)
        || !kit_muduo::ParsePositiveArithmetic(std::string(values[2]), parsed.p)
        || parsed.n != kScryptN
        || parsed.r != kScryptR
        || parsed.p != kScryptP)
    {
        return false;
    }

    return DecodeHex(values[3], parsed.salt.data(), parsed.salt.size())
        && DecodeHex(values[4], parsed.digest.data(), parsed.digest.size());
}

std::string HashWithOpenSSL(const std::string &password)
{
    std::array<unsigned char, kSaltSize> salt{};
    if(RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
    {
        throw std::runtime_error("RAND_bytes failed");
    }

    std::array<unsigned char, kDigestSize> digest{};
    if(EVP_PBE_scrypt(password.data(), password.size(), salt.data(), salt.size(),
                      kScryptN, kScryptR, kScryptP, kScryptMaxMemory,
                      digest.data(), digest.size()) != 1)
    {
        throw std::runtime_error("EVP_PBE_scrypt failed");
    }

    return std::string(kCurrentHashPrefix)
        + std::to_string(kScryptN) + "$"
        + std::to_string(kScryptR) + "$"
        + std::to_string(kScryptP) + "$"
        + EncodeHex(salt.data(), salt.size()) + "$"
        + EncodeHex(digest.data(), digest.size());
}

bool VerifyWithOpenSSL(const std::string &password, const std::string &hash)
{
    ParsedCurrentHash parsed;
    if(!ParseCurrentHash(hash, parsed))
    {
        return false;
    }

    std::array<unsigned char, kDigestSize> digest{};
    if(EVP_PBE_scrypt(password.data(), password.size(), parsed.salt.data(), parsed.salt.size(),
                      parsed.n, parsed.r, parsed.p, kScryptMaxMemory,
                      digest.data(), digest.size()) != 1)
    {
        return false;
    }

    return CRYPTO_memcmp(digest.data(), parsed.digest.data(), digest.size()) == 0;
}

bool VerifyWithLegacyOpenSSL(const std::string &password, const std::string &hash)
{
    if(hash.size() != kLegacySha256HashSize)
    {
        return false;
    }

    const unsigned char *salt = reinterpret_cast<const unsigned char *>(hash.data());
    const unsigned char *stored_digest = salt + kSaltSize;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;

    EVP_MD_CTX *context = EVP_MD_CTX_create();
    if(!context)
    {
        return false;
    }

    const bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1
        && EVP_DigestUpdate(context, salt, kSaltSize) == 1
        && EVP_DigestUpdate(context, password.data(), password.size()) == 1
        && EVP_DigestFinal_ex(context, digest.data(), &digest_size) == 1
        && digest_size == kDigestSize
        && CRYPTO_memcmp(digest.data(), stored_digest, kDigestSize) == 0;

    // EVP_MD_CTX_create/destroy keeps this path compatible with OpenSSL 1.0.1.
    EVP_MD_CTX_destroy(context);
    return ok;
}

bool VerifyWithYescrypt(const std::string &password, const std::string &hash)
{
    crypt_data data{};
    char *result = crypt_r(password.c_str(), hash.c_str(), &data);
    return result && result[0] != '*' && hash == result;
}

} // namespace

std::string PasswordHasher::Hash(const std::string &password)
{
    return HashWithOpenSSL(password);
}

bool PasswordHasher::Verify(const std::string &password, const std::string &hash)
{
    if(hash.empty())
    {
        return false;
    }

    if(hash.size() == kLegacySha256HashSize)
    {
        return VerifyWithLegacyOpenSSL(password, hash);
    }
    if(HasPrefix(hash, kLegacyHashPrefix))
    {
        return VerifyWithYescrypt(password, hash);
    }
    return VerifyWithOpenSSL(password, hash);
}

bool PasswordHasher::NeedsRehash(const std::string &hash)
{
    return hash.size() == kLegacySha256HashSize || HasPrefix(hash, kLegacyHashPrefix);
}

} // namespace kit_domain
