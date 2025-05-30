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
#include "base/util.h"

#include <algorithm>

namespace kit_muduo {
namespace http {

static const char kCRCL[] = "\r\n";

// 有限状态机 解析
bool HttpContext::parseRequest(Buffer &buf, TimeStamp receiveTime)
{
    bool ok = true;
    bool hasMore = true;
    assert(kExpectRequestLine == _state);

    while(hasMore)
    {
        if(kExpectRequestLine == _state)
        {
            const char* crlf_pos = findCRLF(buf);
            if(!crlf_pos)
            {
                HTTP_DEBUG() << "not findCRLF" << std::endl;
                hasMore = false;
                continue;
            }
            ok = processRequestLine(buf.peek(), crlf_pos);
            if(!ok)
            {
                hasMore = false;
                continue;
            }
            _request.setReceiveTime(receiveTime);
            _state = kExpectHeaders;
            buf.reset(crlf_pos + 2 - buf.peek());
        }
        else if(kExpectHeaders == _state)
        {
            const char* crlf_pos = findCRLF(buf);
            if(!crlf_pos)
            {
                HTTP_DEBUG() << "not findCRLF" << std::endl;
                hasMore = false;
                continue;
            }
            const char *start = buf.peek();
            const char *colon = std::find(start, crlf_pos, ':');
            if(colon == crlf_pos)
            {
                hasMore = false;
                // 是否是空行
                if(strncmp(buf.peek(), kCRCL, 2) == 0)
                {
                    _state = kGotAll;
                    buf.reset(2);
                    HTTP_INFO() << "http request header parse ok" << std::endl;
                }
                else
                {
                    HTTP_ERROR() << "not colon ':' ; " << start << std::endl;
                }
                continue;

            }
            _request.addHeader(buf.peek(), colon, crlf_pos);
            buf.reset(crlf_pos + 2 - buf.peek());
        }
        else if(kExpectBody == _state)
        {
            //nothing
        }

    }

    return ok;
}


bool HttpContext::processRequestLine(const char *start, const char *end)
{
    const char *space_pos = std::find(start, end, ' ');
    if(space_pos == end)
    {
        HTTP_F_ERROR("http request parse error! %s \n", start);
        return false;
    }

    std::string tmp_str(start, space_pos);
    DelSpaceHelper(tmp_str);
    const auto& method = HttpRequet::Method::FromString(tmp_str);
    if(HttpRequet::Method::kInvaild == method())
    {
        HTTP_F_ERROR("http request parse 'method' error! %s \n", tmp_str.c_str());
        return false;
    }
    _request.setMethod(method);
    HTTP_DEBUG() << "Method: " << method() << ",|" << method.toString()<< "|" << std::endl;

    start = space_pos + 1;
    space_pos = std::find(start, end, ' ');
    if(space_pos == end)
    {
        HTTP_F_ERROR("http request parse 'path' error! %s \n", tmp_str.c_str());
        return false;
    }
    tmp_str.clear();
    tmp_str.assign(start, space_pos);
    DelSpaceHelper(tmp_str);
    _request.setPath(tmp_str);
    HTTP_DEBUG() << "Path:|" << tmp_str << "|" << std::endl;

    start = space_pos + 1;
    tmp_str.clear();
    tmp_str.assign(start, end);
    DelSpaceHelper(tmp_str);
    const auto& version = HttpRequet::Version::FromString(tmp_str);
    _request.setVersion(version);
    HTTP_DEBUG() << "Version: " << version() << ",|" << version.toString() << "|"<< std::endl;

    return true;
}

const char* HttpContext::findCRLF(Buffer &buf) const
{
    return findCRLF(buf.peek(), buf.beginWrite());
}

const char* HttpContext::findCRLF(const char *start, const char *end) const
{
    const char* pos = std::search(start, end, kCRCL, kCRCL + 2);
    return pos == end? nullptr : pos;
}


}
}