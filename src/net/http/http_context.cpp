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
#include "net/http/http_content.h"
#include "net/net_log.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "base/util.h"
#include "net/http/http_parser.h"

#include <algorithm>
#include "net/http/http_context.h"


using namespace kit_muduo;

namespace kit_muduo {
namespace http {

HttpContext::HttpContext()
    :state_(kExpectRequestLine)
    ,request_(std::make_shared<HttpRequest>())
    ,response_(std::make_shared<HttpResponse>())
    ,parser_(std::make_shared<LLhttpParser>(this))
    ,maybeUpgrade_(false)
{

}


// 有限状态机 解析
bool HttpContext::parseRequest(Buffer &buf, TimeStamp receiveTime)
{
    parser_->setType(HttpParser::ReqType);
    bool ok = parser_->parse(buf);
    if(ok)
    {
        request_->setReceiveTime(receiveTime);
    }

    return ok;
}

bool HttpContext::parseRequest(const std::string &data, TimeStamp receiveTime)
{
    parser_->setType(HttpParser::ReqType);
    bool ok = parser_->parse(data);
    if(ok)
    {
        request_->setReceiveTime(receiveTime);
    }

    return ok;
}


bool HttpContext::parseResponse(Buffer &buf, TimeStamp receiveTime)
{
    parser_->setType(HttpParser::RespType);
    bool ok = parser_->parse(buf);
    if(ok)
    {
        response_->setReceiveTime(receiveTime);
    }
    return ok;
}

bool HttpContext::parseResponse(const std::string &data, TimeStamp receiveTime)
{
    parser_->setType(HttpParser::RespType);
    bool ok = parser_->parse(data);
    if(ok)
    {
        response_->setReceiveTime(receiveTime);
    }
    return ok;
}
ContentView HttpContext::makeContentView() const
{
    const auto &body_data = request_->bodyData();
    
    return {
        .data = body_data.data(),
        .size = body_data.size(),
        .meta = request_->contentMeta(),
    };
}

}
}
