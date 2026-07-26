/**
 * @file http_parser.cpp
 * @brief HTTP报文解析器-自定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 20:39:58
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/http/http_parser.h"
#include "net/http/http_content.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"
#include "net/buffer.h"
#include "net/net_log.h"
#include "net/call_backs.h"

#include <charconv>
#include <cstdint>
#include <limits>

namespace kit_muduo {
namespace http {

namespace {
static const char kCRLF[] = "\r\n";

bool ParseHeaderOneLine(const char *start, const char *colon, const char *end, std::string &head, std::string &val)
{
    assert(start != end);
    head.assign(start, colon);
    DelSpaceHelper(head);
    // assert(head.size() != 0);
    if(head.size() <= 0)
    {
        return false;
    }
    ++colon;
    val.assign(colon, end);
    DelSpaceHelper(val);
    // assert(val.size() != 0);
    HTTP_F_DEBUG("Header: |%s|-|%s|\n", head.c_str(), val.c_str());
    if(val.size() <= 0)
    {
        return false;
    }
    return true;
}


}


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
    HttpRequestPtr request = context_->request();
    HttpResponsePtr response = context_->response();

    while(has_more)
    {
        if(HttpContext::kExpectRequestLine == context_->state())
        {
            const char* crlf_pos = findCRLF(buf);
            if(!crlf_pos)
            {
                if(buf.readableBytes() > limits_.max_start_line_bytes)
                {
                    ok = fail(HttpParseError::kStartLineTooLarge);
                    break;
                }

                HTTP_DEBUG() << "not findCRLF" << "\n";
                has_more = false;
                continue;
            }

            size_t start_line_bytes = static_cast<size_t>(crlf_pos - buf.peek());
            if(!TryConsume(start_line_bytes, sizeof(kCRLF) - 1, limits_.max_start_line_bytes))
            {
                ok = fail(HttpParseError::kStartLineTooLarge);
                break;
            }

            ok = processRequestLine(buf.peek(), crlf_pos);
            if(!ok)
            {
                context_->setParseError(HttpParseError::kInvalidFormat);
                has_more = false;
                continue;
            }
            context_->setState(HttpContext::kExpectHeaders);
            buf.reset(crlf_pos + 2 - buf.peek());
        }
        else if(HttpContext::kExpectHeaders == context_->state())
        {
            const char* crlf_pos = findCRLF(buf);
            if(!crlf_pos)
            {
                if(!TryConsume(pending_header_bytes_, buf.readableBytes() - pending_header_bytes_, limits_.max_header_bytes))
                {
                    ok = fail(HttpParseError::kHeadersTooLarge);
                    break;
                }
                has_more = false;
                continue;
            }
            pending_header_bytes_ = 0;
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
                    std::string transfer_encoding;
                    if(ReqType == type_)
                    {
                        content_len_str = request->getHeader("Content-Length");
                        transfer_encoding = request->getHeader("Transfer-Encoding");
                        const std::string& content_type_str = request->getHeader("Content-Type");
                        request->setContentMeta(ParseHttpContentType(content_type_str));

                        // 特殊处理 Upgrade
                        if(HeaderContainsToken(request->getHeader("Connection"), "Upgrade"))
                        {
                            context_->setMaybeUpgrade(true);
                        }
                    }
                    else if(RespType == type_)
                    {
                        content_len_str = response->getHeader("Content-Length");
                        transfer_encoding = response->getHeader("Transfer-Encoding");
                        const std::string& content_type_str = response->getHeader("Content-Type");
                        response->setContentMeta(ParseHttpContentType(content_type_str));
                    }

                    // 自定义解析器没有实现 chunked framing，不能把剩余 chunk bytes
                    // 当作下一条 HTTP 报文继续处理。
                    if(!transfer_encoding.empty())
                    {
                        ok = fail(HttpParseError::kUnsupportedTransferEncoding);
                        break;
                    }

                    if(content_len_str.empty())
                    {
                        has_more = false;
                        context_->setState(HttpContext::kGotAll);
                        continue;
                    }

                    size_t content_length = 0;
                    if(!parseContentLength(content_len_str, content_length))
                    {
                        ok = false;
                        break;
                    }

                    expected_body_len_ = read_len_ = content_length;
                    if(expected_body_len_ == 0)
                    {
                        HTTP_INFO() << "no body!" << "\n";
                        has_more = false;
                        context_->setState(HttpContext::kGotAll);
                        continue;
                    }

                    context_->setState(HttpContext::kExpectBody);
                }
                else
                {
                    HTTP_ERROR() << "not colon ':' ; "
                        << std::string(start, crlf_pos) << "\n";
                    ok = fail(HttpParseError::kInvalidFormat);
                    has_more = false;
                }
                continue;
            }

            if(!TryConsume(header_count_, 1, limits_.max_header_count))
            {
                ok = fail(HttpParseError::kHeadersTooMany);
                break;
            }

            std::string header_name(start, colon);
            DelSpaceHelper(header_name);
            if(IsHeaderName(header_name, "Content-Length"))
            {
                if(has_content_length_)
                {
                    ok = fail(HttpParseError::kInvalidFormat);
                    break;
                }
                has_content_length_ = true;
            }

            bool header_ok = false;
            std::string head, val;
            header_ok = ParseHeaderOneLine(buf.peek(), colon, crlf_pos, head, val);
            if(!header_ok)
            {
                ok = fail(HttpParseError::kInvalidFormat);
                break;
            }
            if(!TryConsume(header_bytes_, head.size() + val.size(), limits_.max_header_bytes))
            {
                ok = fail(HttpParseError::kHeadersTooLarge);
                break;
            }

            if(ReqType == type_)
            {
                request->addHeader(head, val);
            }
            else if(RespType == type_)
            {
                response->addHeader(head, val);
            }
            buf.reset(crlf_pos + 2 - buf.peek());
        }
        else if(HttpContext::kExpectBody == context_->state())
        {
            size_t min_len = std::min(read_len_, buf.readableBytes());
            if(!TryConsume(body_bytes_, min_len, limits_.max_body_bytes))
            {
                ok = fail(HttpParseError::kBodyTooLarge);
                break;
            }

            if(ReqType == type_)
            {
                request->appendBodyData(buf.peek(), min_len);
            }
            else if(RespType == type_)
            {
                response->appendBodyData(buf.peek(), min_len);
            }

            read_len_ -= min_len;
            buf.reset(min_len);

            if(read_len_ <= 0)
            {
                HTTP_F_DEBUG("body parse ok! len[%zu] \n", expected_body_len_);
                expected_body_len_ = read_len_ = 0;
                has_more = false;
                context_->setState(HttpContext::kGotAll);
            }
            else if(min_len == 0)
            {
                // 当前分片没有更多 Body，等待下一次 parse 调用补齐数据。
                has_more = false;
            }
        }
    }

    if(!ok)
    {
        captureParseError(buf);
    }

    return ok;
}

bool CustomHttpParser::fail(HttpParseError error)
{
    context_->setParseError(error);
    return false;
}

bool CustomHttpParser::parseContentLength(const std::string& value, size_t& content_length)
{
    uint64_t parsed = 0;
    if(!ParsePositiveArithmetic(value, parsed))
    {
        return fail(HttpParseError::kInvalidFormat);
    }

    if(parsed > std::numeric_limits<size_t>::max()
        || parsed > limits_.max_body_bytes)
    {
        return fail(HttpParseError::kBodyTooLarge);
    }
    content_length = static_cast<size_t>(parsed);
    return true;
}

void CustomHttpParser::captureParseError(Buffer& buf)
{
    auto& raw_capture = context_->rawCapture();
    if(raw_capture.size() >= limits_.max_error_capture_bytes)
    {
        return;
    }

    const size_t remaining = limits_.max_error_capture_bytes - raw_capture.size();
    const size_t capture_bytes = std::min(remaining, buf.readableBytes());
    raw_capture.insert(raw_capture.end(), buf.peek(), buf.peek() + capture_bytes);
}


bool CustomHttpParser::processRequestLine(const char *start, const char *end)
{
    HttpRequestPtr request = context_->request();
    HttpResponsePtr response = context_->response();

    // 第一个字段: method（请求） / version（响应）
    const char *space_pos = std::find(start, end, ' ');
    if(space_pos == end)
    {
        HTTP_F_ERROR("http parse first line error! %.*s \n",
            static_cast<int>(end - start), start);
        return false;
    }

    if(ReqType == type_)
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
        HTTP_DEBUG() << "Method: " << method() << ",|" << method.toStr() << "|" << "\n";
    }
    else if(RespType == type_)
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
        HTTP_DEBUG() << "Version: " << version() << ",|" << version.toStr() << "|" << "\n";
    }

    // 第二个字段: path（请求） / status code（响应）
    start = space_pos + 1;
    space_pos = std::find(start, end, ' ');

    std::string tmp_str{start, space_pos};
    DelSpaceHelper(tmp_str);

    if(ReqType == type_)
    {
        if(space_pos == end)
        {
            HTTP_F_ERROR("http request parse 'path' error! %.*s \n",
                static_cast<int>(end - start), start);
            return false;
        }
        request->setPath(tmp_str);
        HTTP_DEBUG() << "Path:|" << tmp_str << "|" << "\n";

        // 第三个字段: version（请求）
        start = space_pos + 1;
        tmp_str.assign(start, end);
        DelSpaceHelper(tmp_str);
        const auto& version = Version::FromString(tmp_str);
        if(Version::kUnknow == version())
        {
            HTTP_F_ERROR("http request parse 'version' error! %s \n", tmp_str.c_str());
            return false;
        }
        request->setVersion(version);
        HTTP_DEBUG() << "Version: " << version() << ",|" << version.toStr() << "|" << "\n";
    }
    else if(RespType == type_)
    {
        // reason phrase 可选，space_pos == end 是合法的（如 "HTTP/1.1 200"）
        const auto& state_code = StateCode::FromString(tmp_str);
        if(StateCode::kUnknow == state_code.toInt())
        {
            HTTP_F_ERROR("http response parse 'status code' error! %s \n", tmp_str.c_str());
            return false;
        }
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
