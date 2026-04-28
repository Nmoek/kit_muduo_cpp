/**
 * @file http_response.h
 * @brief HTTP响应
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 15:04:50
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_RESPONSE_H__
#define __KIT_HTTP_RESPONSE_H__

#include "net/http/http_util.h"
#include "net/buffer.h"
#include "base/time_stamp.h"

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <memory>

namespace kit_muduo {

class ContentParser;

namespace http {

class HttpResponse
{
public:

    HttpResponse();
    ~HttpResponse();

    StateCode stateCode() const { return state_code_; }
    void setStateCode(int32_t val) { state_code_.set(val); }
    void setStateCode(StateCode stateCode) { state_code_ = std::move(stateCode); }

    Version version() const { return version_; }
    void setVersion(int32_t versionVal) { version_.set(versionVal); }
    void setVersion(Version version) { version_ = std::move(version); }

    void addHeader(const std::string& head, const std::string &val);
    bool addHeader(const char *start, const char *colon, const char *end);
    std::string getHeader(const std::string &key) const;

    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }
    const std::unordered_map<std::string, std::string>& headers() { return headers_; }
    void setHeaders(const std::unordered_map<std::string, std::string> &headers) { headers_ = headers; }

    void setConnectionClosed(bool on) { connection_closed_ = on; }
    bool connectionClosed() const { return connection_closed_; }

    void setReceiveTime(TimeStamp receiveTime) { receive_time_ = receiveTime; }
    TimeStamp receiveTime() const { return receive_time_; }
    TimeStamp receiveTime() { return receive_time_; }

    Body& body() { return body_; }
    void setBody(const Body &body) { body_ = body; }


    std::string toString();

protected:
    /// @brief 状态码
    StateCode state_code_;
    /// @brief 协议版本
    Version version_;
    /// @brief 头部字段
    std::unordered_map<std::string, std::string> headers_;
    /// @brief 连接是否关闭
    bool connection_closed_;
    /// @brief Body结构
    Body body_;
    /// @brief 收到响应时间
    TimeStamp receive_time_;
};


}
}
#endif