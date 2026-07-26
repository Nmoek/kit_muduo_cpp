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
#include <unordered_map>


namespace kit_muduo::http {

/***********这几个接口 暂时这么用 后续进一步封装**********/
bool IsHeaderName(const std::string& actual, const std::string& expected);

std::string GetHeaderIgnoreCase(
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& key);

void SetOrReplaceHeader(std::unordered_map<std::string, std::string>& headers,
                        const std::string& canonical_key,
                        const std::string& value);

void EraseHeader(std::unordered_map<std::string, std::string>& headers,
                 const std::string& key);

/***********这几个接口 暂时这么用 后续进一步封装**********/

/**
 * @brief 判断某个 HTTP header的值里是否包含指定 token
 * @param header_value 
 * @param token 
 * @return true 
 * @return false 
 */
bool HeaderContainsToken(const std::string &header_value, const std::string &token);

/**
 * @brief 规整 HTTP path 中连续的 /，用于路由注册和匹配。
 *
 * 示例：
 *   //api///v1  -> /api/v1
 *   /           -> /
 */
std::string NormalizeHttpPath(const std::string &path);


struct Version
{
    enum { kUnknow, kHttp10, kHttp11, kRtsp10};


    explicit Version(int32_t version = kUnknow): m_version(version) { }

    int32_t operator()() const { return m_version; }

    void set(int32_t val) { m_version = val; }

    std::string toString() const { return toStr(); }

    const char* toStr() const
    {
        switch (m_version)
        {
            case kHttp10: return "HTTP/1.0";
            case kHttp11: return "HTTP/1.1";
            case kRtsp10: return "RTSP/1.0";
            
            default:
                return "";
        }
        return "";
    }

    int32_t toInt() const { return m_version; }

    static Version FromString(const std::string &versionStr)
    {
        if("HTTP/1.0" == versionStr) return Version(kHttp10);
        if("HTTP/1.1" == versionStr) return Version(kHttp11);
        if("RTSP/1.0" == versionStr) return Version(kRtsp10);

        return Version();
    }

private:
    int32_t m_version{kUnknow};
};

/**
 * @brief 响应状态码
 */
struct StateCode
{
    enum
    {
        kUnknow = 0,
        //1xx
        k100Continue = 100,
        k101SwitchingProtocols,
        k102Processing,
        //2XX
        k200Ok = 200,
        k204NoContent = 204, 
        //3XX
        k301MovedPermanently = 301,
        k302MoveTemporarily = 302,
        //4XX
        k400BadRequest = 400,
        k401Unauthorized = 401,
        k403Forbidden = 403,
        k404NotFound = 404,
        k405MethodNotAllowed = 405,
        k413PayloadTooLarge = 413,
        k414URITooLong = 414,
        k431RequestHeaderFieldsTooLarge = 431,
        k454SessionNotFound = 454,
        k455MethodNotValid = 455,
        //5XX
        k500InternalServerError = 500,
        k503ServiceUnavailable = 503,
        kMax,
    };

    explicit StateCode(int32_t code = kUnknow): m_code(code) { };
    ~StateCode() = default;

    int32_t operator()() const { return m_code; }

    void set(int32_t val) 
    {
        m_code = val; 
    }

    std::string toString() const
    {
        return m_code >= 100 && m_code < 600 ? std::to_string(m_code) : "";
    }
    
    int32_t toInt() const { return m_code; }

    static StateCode FromString(const std::string &str)
    {
        int32_t code = std::atoi(str.c_str());
        auto it = s_m_codeMessageMap.find(code);

        return it == s_m_codeMessageMap.end() ? StateCode() : StateCode(code);
    }

    std::string message() const
    {
        return s_m_codeMessageMap[m_code];
    }
private:
    static std::unordered_map<int32_t, std::string> s_m_codeMessageMap;

private:
    int32_t m_code;
    std::string m_message;
};


}

#endif
