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

#include "net/http/http_content.h"
#include "net/http/http_util.h"
#include "net/buffer.h"
#include "base/time_stamp.h"
#include "nlohmann/json.hpp"

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
    void setHeaders(const std::unordered_map<std::string, std::string> &headers);

    void setConnectionClosed(bool on) { connection_closed_ = on; }
    bool connectionClosed() const { return connection_closed_; }
    void setUpgrade(bool f) { is_upgrade_ = f;}
    bool upgrade() const { return is_upgrade_; }
    void setSecWebSocketAccept(const std::string &accept)
    {
        if(is_upgrade_) 
        { addHeader("Sec-WebSocket-Accept", accept); }
    }

    void setReceiveTime(TimeStamp receiveTime) { receive_time_ = receiveTime; }
    TimeStamp receiveTime() const { return receive_time_; }
    TimeStamp receiveTime() { return receive_time_; }

    const ContentMeta& contentMeta() const { return content_meta_; }
    ContentMeta& contentMeta() { return content_meta_; }
    void setContentMeta(const ContentMeta& meta);
    void setContentMeta(ContentMeta&& meta);

    const std::vector<uint8_t>& bodyData() const { return body_data_; }
    std::vector<uint8_t>& bodyData() { return body_data_; }
    void setBodyData(const std::vector<uint8_t>& data) { body_data_ = data; }
    void setBodyData(std::vector<uint8_t>&& data) { body_data_ = std::move(data); }
    void setBodyData(const std::vector<char>& data);
    void setBodyData(const std::string& data);
    void appendBodyData(const char* start, size_t len);
    void appendBodyData(const std::string& data);
    void appendBodyData(const std::vector<char>& data);
    void appendBodyData(const std::vector<uint8_t>& data);
    void resetBodyData() { body_data_.clear(); }
    std::string bodyString() const;

    void setJson(const nlohmann::json& root);
    void setText(const std::string& text);
    void setOctetStream(std::vector<uint8_t> data);

    std::string toHeaderString() const;
    std::vector<uint8_t> toBytes() const;
    std::string toString() const;

protected:
    /// @brief 状态码
    StateCode state_code_;
    /// @brief 协议版本
    Version version_;
    /// @brief 头部字段
    std::unordered_map<std::string, std::string> headers_;
    /// @brief 连接是否关闭
    bool connection_closed_;
    /// @brief 连接是否升级
    bool is_upgrade_;
    /// @brief Content-Type 元数据
    ContentMeta content_meta_;
    /// @brief Body原始字节
    std::vector<uint8_t> body_data_;
    /// @brief 收到响应时间
    TimeStamp receive_time_;
};


}
}
#endif
