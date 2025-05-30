/**
 * @file http_util.h
 * @brief HTTP公共部分
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 15:14:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_UTIL_H__
#define __KIT_HTTP_UTIL_H__

#include <bits/stdint-intn.h>
#include <string>
#include <assert.h>

namespace kit_muduo {
namespace http {


struct Version
{
    enum { kUnknow, kHttp10, kHttp11 };


    explicit Version(int32_t version = kUnknow): m_version(version) { }
    int32_t operator()() const { return m_version; }

    void set(int32_t val) { m_version = val; }

    const char* toString() const
    {
        assert(m_version != kUnknow);
        switch (m_version)
        {
            case kHttp10: return "HTTP/1.0";
            case kHttp11: return "HTTP/1.1";
            default:
                return "Unknow";
        }
        return "Unknow";
    }

    static Version FromString(const std::string &versionStr)
    {
        if("HTTP/1.0" == versionStr) return Version(kHttp10);
        if("HTTP/1.1" == versionStr) return Version(kHttp11);
        return Version();
    }

private:
    int32_t m_version{kUnknow};
};


}
}
#endif