/**
 * @file http_response.h
 * @brief HTTP响应
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 15:04:50
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_RESPONSE_H__
#define __KIT_HTTO_RESPONSE_H__

#include "net/http/http_util.h"
#include "net/buffer.h"
#include "base/time_stamp.h"

#include <vector>
#include <algorithm>

namespace kit_muduo {
namespace http {

class HttpResponse
{
public:
    struct StateCode
    {
        enum
        {
            kUnknown,
            //2XX
            k200Ok = 200,
            //3XX
            k301MovedPermanently = 301,
            k302MoveTemporarily = 302,
            //4XX
            k400BadRequest = 400,
            k403Forbidden = 403,
            k404NotFound = 404,
            //5XX
            k500InternalServerError = 500,
        };

        explicit StateCode(int32_t code = kUnknown): m_code(code) { };
        ~StateCode() = default;

        int32_t operator()() const { return m_code; }

        void set(int32_t val) { m_code = val; }

        std::string toString() const
        {
            return std::to_string(m_code);
        }

        static StateCode FromString(const std::string &str)
        {
            int32_t code = std::stoi(str);
            auto it = s_m_codeMessageMap.find(code);

            return it == s_m_codeMessageMap.end() ? StateCode() : StateCode(code);
        }

        std::string message() const
        {
            return m_code == kUnknown ? "UNKNOW" : s_m_codeMessageMap[m_code];
        }
    private:
        static std::unordered_map<int32_t, std::string> s_m_codeMessageMap;
    private:
        int32_t m_code;
        std::string m_message;
    };

    HttpResponse() = default;
    ~HttpResponse() = default;

    StateCode stateCode() const { return _stateCode; }
    void setStateCode(int32_t val) { _stateCode.set(val); }
    void setStateCode(StateCode stateCode) { _stateCode = std::move(stateCode); }

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

    std::string body() const { return _body; }

    std::string toString();

private:
    /// @brief 状态码
    StateCode _stateCode;
    /// @brief 协议版本
    Version _version;
    /// @brief 头部字段
    std::unordered_map<std::string, std::string> _headers;
    /// @brief Body数据
    std::string _body;
    /// @brief 接收时间
    TimeStamp _receiveTime;
};


}
}
#endif