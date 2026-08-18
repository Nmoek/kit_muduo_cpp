/**
 * @file http_context.h
 * @brief HTTP上下文
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 20:18:00
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_CONTEXT_H__
#define __KIT_HTTP_CONTEXT_H__

#include "net/http/http_content_codec.h"
#include "net/http/http_request.h"
#include "net/call_backs.h"
#include "net/http/http_parser.h"

#include <memory>
#include <atomic>
#include <string>
#include <unordered_map>

namespace kit_muduo {

class Buffer;
class TimeStamp;

namespace http {

class HttpContext
{
public:
    /**
     * @brief 解析有限状态机
     */
    enum HttpParseState
    {
        kExpectRequestLine,
        kExpectHeaders,
        kExpectBody,
        kGotAll,
    };


    HttpContext();
    ~HttpContext() = default;

    bool parseRequest(const std::string &data, TimeStamp receiveTime);
    bool parseRequest(Buffer &buf, TimeStamp receiveTime);

    bool parseResponse(const std::string &data, TimeStamp receiveTime);
    bool parseResponse(Buffer &buf, TimeStamp receiveTime);
    HttpParseState state() const { return state_; }
    void setState(HttpParseState state) { state_ = state; }

    void setParseError(HttpParseError error) { error_ = error; }
    HttpParseError parseError() const { return error_; }

    bool gotAll() const { return kGotAll == state_; }

    HttpRequestPtr request() { return request_; }
    HttpResponsePtr response() { return response_; }

    std::string routeParam(const std::string& key) const
    {
       return request_->getRouteParam(key);
    }

    std::string queryParam(const std::string& key) const
    {
       return request_->getQureyParam(key);
    }

    void setAttribute(const std::string &key, const std::string &value)
    {
        attributes_[key] = value;
    }

    std::string attribute(const std::string &key) const
    {
        auto it = attributes_.find(key);
        return it == attributes_.end() ? "" : it->second;
    }

    const std::vector<uint8_t>& rawCapture() const { return raw_capture_; }
    std::vector<uint8_t>& rawCapture() { return raw_capture_; }

    void setMaybeUpgrade(bool flag) { maybeUpgrade_ = flag; }
    bool maybeUpgrade() const { return maybeUpgrade_; }

    template<typename T>
    kit_muduo::http::ContentCodecResult bindJson(T &obj)
    {
        return kit_muduo::http::ContentDecodePipeline<T>::Decode(makeContentView(), obj, {kit_muduo::http::ContentCodecFormat::kJson});
    }

    template<typename T>
    kit_muduo::http::ContentCodecResult bindMultipart(T &obj)
    {
        return kit_muduo::http::ContentDecodePipeline<T>::Decode(makeContentView(), obj, {kit_muduo::http::ContentCodecFormat::kMultipartFormData});
    }


private:
    kit_muduo::http::ContentView makeContentView() const;

private:
    /// @brief HTTP报文解析状态
    HttpParseState  state_{kExpectRequestLine};
    /// @brief HTTP报文解析错误提示
    HttpParseError error_{HttpParseError::kNone};
    /// @brief HTTP请求报文
    HttpRequestPtr request_;
    /// @brief  HTTP响应报文
    HttpResponsePtr response_;
    /// @brief HTTP报文解析器
    std::shared_ptr<HttpParser> parser_;
    std::unordered_map<std::string, std::string> attributes_;
    /// @brief 简单判断是否应该走upgrade路径
    bool maybeUpgrade_{false};
    /// @brief 用于解析失败捕获raw bytes
    std::vector<uint8_t> raw_capture_;
};




}
}
#endif
