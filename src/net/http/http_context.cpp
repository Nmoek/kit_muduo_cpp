/**
 * @file http_context.cpp
 * @brief HTTP上下文
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 22:25:23
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_context.h"
#include "net/buffer.h"
#include "net/net_log.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "base/util.h"
#include "net/http/http_parser.h"

#include <algorithm>
#include "net/http/http_context.h"

namespace kit_muduo {
namespace http {

HttpContext::HttpContext()
    :_state(kExpectRequestLine)
    ,_request(std::make_shared<HttpRequest>())
    ,_response(std::make_shared<HttpResponse>())
    ,_parser(std::make_shared<LLhttpParser>(this)) 
{
    HTTP_DEBUG() << "HttpContext constructor " << this << std::endl;
}
HttpContext::~HttpContext()
{
    HTTP_DEBUG()  << "~HttpContext " << this << std::endl;
}

// 有限状态机 解析
bool HttpContext::parseRequest(Buffer &buf, TimeStamp receiveTime)
{
    _parser->setType(HttpParser::ReqType);
    bool ok = _parser->parse(buf);
    if(ok)
    {
        _request->setReceiveTime(receiveTime);
    }

    return ok;
}

bool HttpContext::parseRequest(const std::string &data, TimeStamp receiveTime)
{
    _parser->setType(HttpParser::ReqType);
    bool ok = _parser->parse(data);
    if(ok)
    {
        _request->setReceiveTime(receiveTime);
    }

    return ok;
}


bool HttpContext::parseResponse(Buffer &buf, TimeStamp receiveTime)
{
    _parser->setType(HttpParser::RespType);
    bool ok = _parser->parse(buf);
    if(ok)
    {
        _response->setReceiveTime(receiveTime);
    }
    return ok;
}

bool HttpContext::parseResponse(const std::string &data, TimeStamp receiveTime)
{
    _parser->setType(HttpParser::RespType);
    bool ok = _parser->parse(data);
    if(ok)
    {
        _response->setReceiveTime(receiveTime);
    }
    return ok;
}


}
}