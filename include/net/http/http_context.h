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

#include "net/http/http_request.h"
#include "net/call_backs.h"
#include "base/content_parser.h"

#include <memory>
#include <atomic>
#include <string>

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

    /**
     * @brief  从HttpRequest中自动根据Content-Type解析出body
     * @param[in] body 
     * @return true 
     * @return false 
     */
    template<typename T>
    bool Bind(T *obj)
    {
        int32_t type = _request->body().contentType()();

        // 有点类似 配置系统设计
        // 同时需要满足 多态 + 模版
        // 特化与偏特化
        try {

            switch (type)
            {
                case http::ContentType::kJsonType: return BindWithJson<T>(obj); break;
                case http::ContentType::kMultiForm: return BindWithMultiForm<T>(obj);
                case http::ContentType::kXmlType:  break;
    
                default:
                    break;
            }
    
        } catch(std::exception &e) {
            std::cerr << "HttpContext::Bind error! " << e.what() << std::endl;
        }

        return false;
    }
    template<typename T>
    bool BindWithJson(T *obj)
    {
        *obj = nljson::parse(_request->body().data()).get<T>();
        return true;
    }

    template<typename T>
    bool BindWithMultiForm(T *obj)
    {
        const auto &data = _request->body().data();
        auto parts = MultiFormConvert::parse(std::string(data.begin(), data.end()), _request->getHeader("Content-Type"));
        
        return T::from_multi_form(parts, *obj);
    }


    template<typename T>
    bool BindWithXml(T *obj)
    {
        return false;
    }

private:
    /// @brief HTTP请求解析状态
    HttpParseState  _state{kExpectRequestLine};
    /// @brief HTTP请求报文
    HttpRequestPtr _request;
    /// @brief  HTTP响应报文
    HttpResponsePtr _response;
    /// @brief HTTP报文解析器
    std::shared_ptr<HttpParser> _parser;
};




}
}
#endif
