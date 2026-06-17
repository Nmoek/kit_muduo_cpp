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

#include "base/content_codec.h"
#include "net/http/http_request.h"
#include "net/call_backs.h"
#include "base/content_parser.h"

#include <memory>
#include <atomic>
#include <string>
#include <unordered_map>

namespace kit_muduo {

class Buffer;
class TimeStamp;

namespace http {

class HttpParser;

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
    ~HttpContext();

    bool parseRequest(const std::string &data, TimeStamp receiveTime);
    bool parseRequest(Buffer &buf, TimeStamp receiveTime);

    bool parseResponse(const std::string &data, TimeStamp receiveTime);
    bool parseResponse(Buffer &buf, TimeStamp receiveTime);
    HttpParseState state() const { return _state; }
    void setState(HttpParseState state) { _state = state; }

    bool gotAll() const { return kGotAll == _state; }

    HttpRequestPtr request() { return _request; }
    HttpResponsePtr response() { return _response; }

    std::string routeParam(const std::string& key) const
    {
       return _request->getRouteParam(key);
    }

    std::string queryParam(const std::string& key) const
    {
       return _request->getQureyParam(key);
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

    template<typename T>
    kit_muduo::ContentCodecResult bindJson(T *obj)
    {
        return kit_muduo::ContentDecodePipeline<T>::Decode(makeContentView(), obj, {kit_muduo::ContentFormat::kJson});
    }

    template<typename T>
    kit_muduo::ContentCodecResult bindMultipart(T *obj)
    {
        return kit_muduo::ContentDecodePipeline<T>::Decode(makeContentView(), obj, {kit_muduo::ContentFormat::kMultipart});
    }


private:
    kit_muduo::ContentView makeContentView() const;

private:
    /// @brief HTTP请求解析状态
    HttpParseState  _state{kExpectRequestLine};
    /// @brief HTTP请求报文
    HttpRequestPtr _request;
    /// @brief  HTTP响应报文
    HttpResponsePtr _response;
    /// @brief HTTP报文解析器
    std::shared_ptr<HttpParser> _parser;
    std::unordered_map<std::string, std::string> attributes_;
};




}
}
#endif
