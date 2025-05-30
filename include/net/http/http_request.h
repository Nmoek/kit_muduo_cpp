/**
 * @file http_request.h
 * @brief HTTP请求
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 20:19:34
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_REQUEST_H__
#define __KIT_HTTP_REQUEST_H__
#include "base/time_stamp.h"

#include <string>
#include <unordered_map>
#include <assert.h>

namespace kit_muduo {

namespace http {

class HttpRequet
{
public:
    struct Method
    {
        enum { kInvaild, kGet, kPost, kHead, kPut, kDelete };

        Method() = default;
        // 支持隐式转换
        Method(int32_t method) :m_method(method) { }

        int32_t operator()() const { return m_method; }

        const char* toString() const
        {
            assert(m_method != kInvaild);
            switch (m_method)
            {
                case kGet: return "GET";
                case kPost: return "POST";
                case kHead: return "HEAD";
                case kPut: return "PUT";
                case kDelete: return "DELETE";
                default:
                    return "";
            }
            return "";
        }

        static Method FromString(const std::string &methodStr)
        {
            if("GET" == methodStr) return Method::kGet;
            if("POST" == methodStr) return Method::kPost;
            if("HEAD" == methodStr) return Method::kHead;
            if("PUT" == methodStr) return Method::kPut;
            if("DELETE" == methodStr) return Method::kDelete;
            return Method::kInvaild;
        }
    private:
        int32_t m_method{kInvaild};
    };

    struct Version
    {
        enum { kUnknow, kHttp10, kHttp11 };

        Version() = default;

        Version(int32_t version): m_version(version) { }
        int32_t operator()() const { return m_version; }

        const char* toString() const
        {
            assert(m_version != kUnknow);
            switch (m_version)
            {
                case kHttp10: return "HTTP/1.0";
                case kHttp11: return "HTTP/1.1";
                default:
                    return "";
            }
            return "";
        }
        static Version FromString(const std::string &versionStr)
        {
            if("HTTP/1.0" == versionStr) return Version::kHttp10;
            if("HTTP/1.1" == versionStr) return Version::kHttp11;
            return Version::kUnknow;
        }

    private:
        int32_t m_version{kUnknow};
    };

    HttpRequet() = default;
    ~HttpRequet() = default;

    Method method() const { return _method; }
    void setMethod(int32_t methodVal) { _method = methodVal; }
    void setMethod(Method method) { _method = method; }


    std::string path() const { return _path; }
    void setPath(const std::string &path) { _path = path; }

    std::string qurey() const { return _query; }
    void setQurey(const std::string &qurey) { _query = qurey; }


    Version version() const { return _version; }
    void setVersion(int32_t versionVal) { _version = versionVal; }
    void setVersion(Version version) { _version = version; }

    void addHeader(const char *start, const char *colon, const char *end);
    std::string getHeader(const std::string &key) const;

    const std::unordered_map<std::string, std::string>& headers() const { return _headers; }
    const std::unordered_map<std::string, std::string>& headers() { return _headers; }

    void setReceiveTime(TimeStamp receiveTime) { _receiveTime = receiveTime; }
    TimeStamp receiveTime() const { return _receiveTime; }
    TimeStamp receiveTime() { return _receiveTime; }


private:
    /// @brief 请求方法
    Method _method;
    /// @brief 请求路径
    std::string _path;
    /// @brief 请求参数
    std::string _query;
    /// @brief 协议版本
    Version _version;
    /// @brief 头部字段
    std::unordered_map<std::string, std::string> _headers;
    /// @brief 接收请求时间点
    TimeStamp _receiveTime;

};


}
}
#endif