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
#include "net/http/http_util.h"
#include "net/buffer.h"

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

        explicit Method(int32_t method = kInvaild) :m_method(method) { }

        int32_t operator()() const { return m_method; }

        void set(int32_t val) { m_method = val; }

        const char* toString() const
        {
            switch (m_method)
            {
                case kGet: return "GET";
                case kPost: return "POST";
                case kHead: return "HEAD";
                case kPut: return "PUT";
                case kDelete: return "DELETE";
                default:
                    return "Invaild";
            }
            return "Invaild";
        }

        static Method FromString(const std::string &methodStr)
        {
            if("GET" == methodStr) return Method(kGet);
            if("POST" == methodStr) return Method(kPost);
            if("HEAD" == methodStr) return Method(kHead);
            if("PUT" == methodStr) return Method(kPut);
            if("DELETE" == methodStr) return Method(kDelete);
            return Method();
        }
    private:
        int32_t m_method{kInvaild};
    };

    HttpRequet() = default;
    ~HttpRequet() = default;

    Method method() const { return _method; }
    void setMethod(int32_t methodVal) { _method.set(methodVal); }
    void setMethod(Method method) { _method = std::move(method); }


    std::string path() const { return _path; }
    void setPath(const std::string &path) { _path = path; }

    std::string qurey() const { return _query; }
    void setQurey(const std::string &qurey) { _query = qurey; }


    Version version() const { return _version; }
    void setVersion(int32_t versionVal) { _version.set(versionVal); }
    void setVersion(Version version) { _version = std::move(version); }

    void addHeader(const std::string& head, const std::string &val);
    void addHeader(const char *start, const char *colon, const char *end);
    std::string getHeader(const std::string &key) const;

    const std::unordered_map<std::string, std::string>& headers() const { return _headers; }
    const std::unordered_map<std::string, std::string>& headers() { return _headers; }

    void appendBody(const std::string &data)
    {
        _body = data;
    }

    void appendBody(const char *start, size_t len)
    {
        _body.assign(start, start + len);
    }

    void appendBody(Buffer& buffer)
    {
        _body = buffer.resetAllAsString();
    }

    void setReceiveTime(TimeStamp receiveTime) { _receiveTime = receiveTime; }
    TimeStamp receiveTime() const { return _receiveTime; }
    TimeStamp receiveTime() { return _receiveTime; }

    std::string body() const { return _body; };

    /**
     * @brief 报文序列化
     * @return std::string
     */
    std::string toString();


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
    /// @brief Body数据
    std::string _body;
    /// @brief 接收请求时间点
    TimeStamp _receiveTime;

};


}
}
#endif