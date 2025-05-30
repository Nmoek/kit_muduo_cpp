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

    HttpContext() = default;
    ~HttpContext() = default;

    bool parseRequest(Buffer &buf, TimeStamp receiveTime);

    bool gotAll() const { return kGotAll == _state; }

    HttpRequet& request() { return _request; }
    const HttpRequet& request() const {return _request; }

private:
    bool processRequestLine(const char *start, const char *end);

    const char* findCRLF(Buffer &buf) const;
    const char* findCRLF(const char *start, const char *end) const;

private:
    /// @brief HTTP请求解析状态
    HttpParseState  _state{kExpectRequestLine};
    /// @brief HTTP请求报文
    HttpRequet _request;
};




}
}
#endif