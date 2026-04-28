/**
 * @file http_parser.cpp
 * @brief HTTP报文解析器-自定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 20:39:58
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_parser.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/buffer.h"
#include "net/net_log.h"
#include "net/call_backs.h"

namespace kit_muduo {
namespace http {

static const char kCRLF[] = "\r\n";

bool CustomHttpParser::parse(const std::string &data)
{
    Buffer buf;
    buf.append(data.data(), data.size());
    return parse(buf);
}

// 有限状态机 解析
bool CustomHttpParser::parse(Buffer &buf)
{
    if(buf.readableBytes() <= 0)
    {
        return false;
    }
    bool ok = true;
    bool has_more = true;
    HttpRequestPtr request = _context->request();
    HttpResponsePtr response = _context->response();

    while(has_more)
    {
        if(HttpContext::kExpectRequestLine == _context->state())
        {
            const char* crlf_pos = findCRLF(buf);
            if(!crlf_pos)
            {
                HTTP_DEBUG() << "not findCRLF" << "\n";
                has_more = false;
                continue;
            }
            ok = processRequestLine(buf.peek(), crlf_pos);
            if(!ok)
            {
                has_more = false;
                continue;
            }
            _context->setState(HttpContext::kExpectHeaders);
            buf.reset(crlf_pos + 2 - buf.peek());
        }
        else if(HttpContext::kExpectHeaders == _context->state())
        {
            const char* crlf_pos = findCRLF(buf);
            if(!crlf_pos)
            {
                HTTP_DEBUG() << "not findCRLF" << "\n";
                has_more = false;
                continue;
            }
            const char *start = buf.peek();
            const char *colon = std::find(start, crlf_pos, ':');
            if(colon == crlf_pos)
            {
                // 是否是空行
                if(strncmp(buf.peek(), kCRLF, 2) == 0)
                {
                    HTTP_INFO() << "http header parse ok" << "\n";
                    buf.reset(2);

                    std::string content_len_str;
                    if(ReqType == _type)
                    {
                        content_len_str = request->getHeader("Content-Length");
                    }
                    else if(RespType == _type)
                    {
                        content_len_str = response->getHeader("Content-Length");
                    }

                    read_len_ = expected_body_len_ = atoi(content_len_str.c_str());

                    if(expected_body_len_ <= 0 || content_len_str.empty())
                    {
                        HTTP_INFO() << "no body!" << "\n";
                        has_more = false;
                        _context->setState(HttpContext::kGotAll);
                        continue;
                    }

                    if(ReqType == _type)
                    {
                        const std::string& content_type_str = request->getHeader("Content-Type");
                        request->body().setContentType(http::ContentType::FromString(content_type_str));
                    }
                    else if(RespType == _type)
                    {
                        const std::string& content_type_str = response->getHeader("Content-Type");
                        response->body().setContentType(http::ContentType::FromString(content_type_str));
                    }

                    _context->setState(HttpContext::kExpectBody);
                }
                else
                {
                    HTTP_ERROR() << "not colon ':' ; " << start << "\n";
                    ok = false;
                    has_more = false;
                }
                continue;
            }
            if(ReqType == _type)
            {
                request->addHeader(buf.peek(), colon, crlf_pos);
            }
            else if(RespType == _type)
            {
                response->addHeader(buf.peek(), colon, crlf_pos);
            }
            buf.reset(crlf_pos + 2 - buf.peek());
        }
        else if(HttpContext::kExpectBody == _context->state())
        {
            size_t min_len = std::min(read_len_, buf.readableBytes());
            read_len_ -= min_len;

            if(ReqType == _type)
            {
                request->body().appendData(buf.peek(), min_len);
            }
            else if(RespType == _type)
            {
                response->body().appendData(buf.peek(), min_len);
            }

            buf.reset(min_len);

            if(read_len_ <= 0)
            {
                HTTP_F_DEBUG("body parse ok! len[%zu] \n", expected_body_len_);
                expected_body_len_ = read_len_ = 0;
                has_more = false;
                _context->setState(HttpContext::kGotAll);
            }
        }
    }

    return ok;
}


bool CustomHttpParser::processRequestLine(const char *start, const char *end)
{
    HttpRequestPtr request = _context->request();
    HttpResponsePtr response = _context->response();

    // 第一个字段: method（请求） / version（响应）
    const char *space_pos = std::find(start, end, ' ');
    if(space_pos == end)
    {
        HTTP_F_ERROR("http parse first line error! %s \n", start);
        return false;
    }

    if(ReqType == _type)
    {
        std::string tmp_str(start, space_pos);
        DelSpaceHelper(tmp_str);
        const auto& method = HttpRequest::Method::FromString(tmp_str);
        if(HttpRequest::Method::kInvaild == method())
        {
            HTTP_F_ERROR("http request parse 'method' error! %s \n", tmp_str.c_str());
            return false;
        }
        request->setMethod(method);
        HTTP_DEBUG() << "Method: " << method() << ",|" << method.toString() << "|" << "\n";
    }
    else if(RespType == _type)
    {
        std::string tmp_str(start, space_pos);
        DelSpaceHelper(tmp_str);
        const auto& version = Version::FromString(tmp_str);
        if(Version::kUnknow == version())
        {
            HTTP_F_ERROR("http response parse 'version' error! %s \n", tmp_str.c_str());
            return false;
        }
        response->setVersion(version);
        HTTP_DEBUG() << "Version: " << version() << ",|" << version.toString() << "|" << "\n";
    }

    // 第二个字段: path（请求） / status code（响应）
    start = space_pos + 1;
    space_pos = std::find(start, end, ' ');

    std::string tmp_str{start, space_pos};
    DelSpaceHelper(tmp_str);

    if(ReqType == _type)
    {
        if(space_pos == end)
        {
            HTTP_F_ERROR("http request parse 'path' error! %s \n", start);
            return false;
        }
        request->setPath(tmp_str);
        HTTP_DEBUG() << "Path:|" << tmp_str << "|" << "\n";

        // 第三个字段: version（请求）
        start = space_pos + 1;
        tmp_str.assign(start, end);
        DelSpaceHelper(tmp_str);
        const auto& version = Version::FromString(tmp_str);
        request->setVersion(version);
        HTTP_DEBUG() << "Version: " << version() << ",|" << version.toString() << "|" << "\n";
    }
    else if(RespType == _type)
    {
        // reason phrase 可选，space_pos == end 是合法的（如 "HTTP/1.1 200"）
        const auto& state_code = StateCode::FromString(tmp_str);
        response->setStateCode(state_code);
        HTTP_DEBUG() << "StateCode: " << state_code.toInt() << "\n";
    }

    return true;
}

const char* CustomHttpParser::findCRLF(Buffer &buf) const
{
    return findCRLF(buf.peek(), buf.beginWrite());
}

const char* CustomHttpParser::findCRLF(const char *start, const char *end) const
{
    std::string crlf{"\r\n"};
    auto pos = std::search(start, end, crlf.begin(), crlf.end());
    return pos == end ? nullptr : pos;
}


}
}   //kit_muduo
