/**
 * @file http_parser.cpp
 * @brief HTTP报文解析器-llhttp库
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 20:39:58
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/util.h"
#include "llhttp.h"
#include "net/http/http_parser.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/buffer.h"
#include "net/http/http_util.h"
#include "net/net_log.h"
#include "net/call_backs.h"


namespace kit_muduo {
namespace http {

namespace {

static bool IsHexDigit(char ch)
{
    return ('0' <= ch && ch <= '9')
        || ('a' <= ch && ch <= 'f')
        || ('A' <= ch && ch <= 'F');
}

static int HexToInt(char ch)
{
    if('0' <= ch && ch <= '9') return ch - '0';
    if('a' <= ch && ch <= 'f') return ch - 'a' + 10;
    if('A' <= ch && ch <= 'F') return ch - 'A' + 10;
    return 0;
}

static std::string UrlDecode(const std::string &value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for(size_t i = 0; i < value.size(); ++i)
    {
        if(value[i] == '%' && i + 2 < value.size()
            && IsHexDigit(value[i + 1]) && IsHexDigit(value[i + 2]))
        {
            decoded.push_back(static_cast<char>(
                HexToInt(value[i + 1]) * 16 + HexToInt(value[i + 2])));
            i += 2;
        }
        else if(value[i] == '+')
        {
            decoded.push_back(' ');
        }
        else
        {
            decoded.push_back(value[i]);
        }
    }

    return decoded;
}


} // namespace

LLhttpParser::LLhttpParser(HttpContext *context)
    :HttpParser(context)
    ,is_paused_(false)
{
    llhttp_settings_init(&settings_);
    settings_.on_message_begin = &LLhttpParser::onMessageBegin;
    settings_.on_method = &LLhttpParser::onMethod;
    settings_.on_status = &LLhttpParser::onStatus;
    settings_.on_status_complete = &LLhttpParser::onStatusComplete;
    settings_.on_url = &LLhttpParser::onUrl;
    settings_.on_url_complete = &LLhttpParser::onUrlComplete;
    settings_.on_version = &LLhttpParser::onVersion;
    settings_.on_header_field = &LLhttpParser::onHeaderField;
    settings_.on_header_field_complete = &LLhttpParser::onHeaderFieldComplete;
    settings_.on_header_value = &LLhttpParser::onHeaderValue;
    settings_.on_header_value_complete = &LLhttpParser::onHeaderValueComplete;
    settings_.on_headers_complete = &LLhttpParser::onHeadersComplete;
    settings_.on_body = &LLhttpParser::onBody;
    settings_.on_message_complete = &LLhttpParser::onMessageComplete;

    llhttp_init(&parser_, HTTP_BOTH, &settings_);

    parser_.data = static_cast<void*>(this);

}

bool LLhttpParser::parse(const std::string &data)
{
    Buffer buf;
    buf.append(data.data(), data.size());
    return parse(buf);
}

bool LLhttpParser::parse(Buffer &buf)
{
    const char *data = buf.peek();
    const size_t len = buf.readableBytes();
    auto &raw_capture = context_->rawCapture();

    if(is_paused_)
    {
        is_paused_ = false;
        llhttp_resume(&parser_);
    }

    // 开启解析
    // 注意需要把\0去掉
    llhttp_errno err = llhttp_execute(&parser_, data, len);

    const char *err_pos = llhttp_get_error_pos(&parser_);
    size_t consumed_len = len;
    if(err_pos != nullptr && err_pos >= data && err_pos <= data + len)
    {
        consumed_len = static_cast<size_t>(err_pos - data);
        assert(consumed_len <= len);
    }

    // 特别注意: HPE_OK不代表解析到完整报文，只代表当前输入的数据没有问题
    if(err == HPE_OK)
    {
        buf.reset(consumed_len);
        return true;
    }

    // 解析到完整报文一定会HPE_PAUSED
    if(err == HPE_PAUSED || err == HPE_PAUSED_UPGRADE)
    {
        is_paused_ = true;
        buf.reset(consumed_len);
        return true;
    }

    if(HttpParseError::kNone == context_->parseError())
    {
        context_->setParseError(HttpParseError::kInvalidFormat);
    }

    // 解析错误时是否 reset consumed 要谨慎。当前上层会 400+shutdown，
    // 可以消费已解析部分，也可以保留给日志。最小改动建议先不保留。
    

    // http捕获 解析失败的原始bytes
    raw_capture.insert(raw_capture.end(), buf.peek(), buf.peek() + std::min(limits_.max_error_capture_bytes, buf.readableBytes()));


    buf.reset(consumed_len);
    return false;
}

int LLhttpParser::onMessageBegin(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    parser_ptr->clear();

    return HPE_OK;
}


int LLhttpParser::onMethod(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->context_->request();

    if(!TryConsume(parser_ptr->start_line_bytes_, len, parser_ptr->limits_.max_start_line_bytes))
    {
        return parser_ptr->fail(parser, HttpParseError::kStartLineTooLarge, "start line too large");
    }

    const std::string &s = std::string(data, len);
    HTTP_DEBUG() << "method: " << s << std::endl;
    
    request->setMethod(HttpRequest::Method::FromString(s));
    return HPE_OK;
}

int LLhttpParser::onStatus(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->context_->request();
    HttpResponsePtr response = parser_ptr->context_->response();

    if(!TryConsume(parser_ptr->start_line_bytes_, len, parser_ptr->limits_.max_start_line_bytes))
    {
        return parser_ptr->fail(parser, HttpParseError::kStartLineTooLarge, "start line too large");
    }

    HTTP_DEBUG() << "status: " << llhttp_get_status_code(parser) << std::endl;

    response->setStateCode(llhttp_get_status_code(parser));

    return HPE_OK;
}

int LLhttpParser::onStatusComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    HTTP_DEBUG() << "response line parse ok" << std::endl;

    // 状态转换
    if(RespType == parser_ptr->type_)
    {
        parser_ptr->context_->setState(HttpContext::kExpectHeaders);
    }

    return 0;
}


int LLhttpParser::onUrl(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HeaderContext &ctx = parser_ptr->head_ctx_;

    if(!TryConsume(parser_ptr->start_line_bytes_, len, parser_ptr->limits_.max_start_line_bytes))
    {
        return parser_ptr->fail(parser, HttpParseError::kStartLineTooLarge, "start line too large");
    }

    std::string s(data, len);
    HTTP_DEBUG() << "url: " << s << std::endl;

    ctx.url.append(data, len);
    
    return HPE_OK;
}

void LLhttpParser::parseQueryParams(const std::string &query, const HttpRequestPtr &request)
{
    size_t start = 0;
    while(start <= query.size())
    {
        const size_t end = query.find('&', start);
        const size_t part_end = end == std::string::npos ? query.size() : end;

        if(part_end > start)
        {
            const size_t equal = query.find('=', start);
            const bool has_equal = equal != std::string::npos && equal < part_end;
            std::string key;
            std::string val;

            if(has_equal)
            {
                key = query.substr(start, equal - start);
                val = query.substr(equal + 1, part_end - equal - 1);
            }
            else
            {
                key = query.substr(start, part_end - start);
            }

            key = UrlDecode(key);
            if(!key.empty())
            {
                request->addQureyParam(key, UrlDecode(val));
            }
        }

        if(end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
}

void LLhttpParser::parseUrl(const std::string &url, const HttpRequestPtr &request)
{
    const size_t query_pos = url.find('?');
    if(query_pos != std::string::npos)
    {
        request->setPath(url.substr(0, query_pos));
        parseQueryParams(url.substr(query_pos + 1), request);
    }
    else
    {
        request->setPath(url);
    }
}

void LLhttpParser::clear()
{
    // raw捕获清除
    context_->rawCapture().clear();
    head_ctx_.cur_header.clear();
    head_ctx_.cur_header_val.clear();
    head_ctx_.url.clear();
    head_ctx_.headers.clear();
    start_line_bytes_ = 0;
    header_bytes_ = 0;
    header_count_ = 0;
    body_bytes_ = 0;
}

int32_t LLhttpParser::fail(llhttp_t *parser, HttpParseError error, const char* reason)
{
    context_->setParseError(error);
    llhttp_set_error_reason(parser, reason);
    return HPE_USER;
}


int LLhttpParser::onUrlComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->context_->request();
    HeaderContext &ctx = parser_ptr->head_ctx_;

    request->setUrl(ctx.url);
    parser_ptr->parseUrl(ctx.url, request);
    return 0;
}

int LLhttpParser::onVersion(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->context_->request();
    HttpResponsePtr response = parser_ptr->context_->response();

    if(!TryConsume(parser_ptr->start_line_bytes_, len, parser_ptr->limits_.max_start_line_bytes))
    {
        return parser_ptr->fail(parser, HttpParseError::kStartLineTooLarge, "start line too large");
    }

    std::string s = "HTTP/";
    s += std::string(data, len);
    HTTP_DEBUG() << "version: " << s << std::endl;

    if(ReqType == parser_ptr->type_)
    {
        request->setVersion(Version::FromString(s));
    }
    else
    {
        response->setVersion(Version::FromString(s));
    }
    return HPE_OK;
}


int LLhttpParser::onVersionComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    HTTP_DEBUG() << "request line parse ok" << std::endl;

    // 状态转换
    parser_ptr->context_->setState(HttpContext::kExpectHeaders);
    return 0;
}

int LLhttpParser::onHeaderField(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    if(!TryConsume(parser_ptr->header_bytes_, len, parser_ptr->limits_.max_header_bytes))
    {
        return parser_ptr->fail(parser, HttpParseError::kHeadersTooLarge, "header too large");
    }

    // 注意 只能追加
    parser_ptr->head_ctx_.cur_header.append(data, len);
    return HPE_OK;
}

int LLhttpParser::onHeaderFieldComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    
    if(!TryConsume(parser_ptr->header_count_, 1, parser_ptr->limits_.max_header_count))
    {
        return parser_ptr->fail(parser, HttpParseError::kHeadersTooMany, "header count too many");
    }

    return HPE_OK;
}

int LLhttpParser::onHeaderValue(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    if(!TryConsume(parser_ptr->header_bytes_, len, parser_ptr->limits_.max_header_bytes))
    {
        parser_ptr->fail(parser, HttpParseError::kHeadersTooLarge, "header too large");
        return HPE_USER;
    }

    // 注意 只能追加
    parser_ptr->head_ctx_.cur_header_val.append(data, len);

    return HPE_OK;
}

int LLhttpParser::onHeaderValueComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HeaderContext &ctx = parser_ptr->head_ctx_;

    if(ctx.cur_header.empty())
    {
        return parser_ptr->fail(parser, HttpParseError::kInvalidFormat, "format invalid");
    }

    ctx.headers[ctx.cur_header] = ctx.cur_header_val;
    ctx.cur_header.clear();
    ctx.cur_header_val.clear();
    return HPE_OK;
}

int LLhttpParser::onHeadersComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HeaderContext &ctx = parser_ptr->head_ctx_;
    HttpRequestPtr request = parser_ptr->context_->request();
    HttpResponsePtr response = parser_ptr->context_->response();

    for(auto &it : ctx.headers)
    {
        HTTP_DEBUG() << it.first << " : " << it.second << std::endl;
    }

    // headers字段赋值
    if(ReqType == parser_ptr->type_)
    {
        request->setHeaders(ctx.headers);
        // 特殊处理 Upgrade
        if(parser->flags & F_UPGRADE)
        {
            parser_ptr->context_->setMaybeUpgrade(true);
        }
    }
    else
    {
        response->setHeaders(ctx.headers);
    }

    // 注意 这里报文结束不代表数据全部收完，body可能被拆包了
    if(0 != (parser->flags & F_CONTENT_LENGTH)
        && parser->content_length > parser_ptr->limits_.max_body_bytes)
    {
        return parser_ptr->fail(parser, HttpParseError::kBodyTooLarge, "body too large");
    }

    // 状态转换
    parser_ptr->context_->setState(HttpContext::kExpectBody);
    return HPE_OK;
}

int LLhttpParser::onBody(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->context_->request();
    HttpResponsePtr response = parser_ptr->context_->response();

    if(!TryConsume(parser_ptr->body_bytes_, len, parser_ptr->limits_.max_body_bytes))
    {
        return  parser_ptr->fail(parser, HttpParseError::kBodyTooLarge, "body too large");
    }

    if(ReqType == parser_ptr->type_)
    {
        request->appendBodyData(data, len);
    }
    else 
    {
        response->appendBodyData(data, len);
    }

    return HPE_OK;
}
// 该回调函数必须设置
int LLhttpParser::onMessageComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->context_->request();
    HttpResponsePtr response = parser_ptr->context_->response();
 

    HTTP_F_INFO("http request/response parse finish! body data size: [%lld/%lld]\n", (ReqType == parser_ptr->type_ ?
        request->bodyData().size() : response->bodyData().size()),
        parser->content_length);
    
    // 解析完成
    parser_ptr->context_->setState(HttpContext::kGotAll);

    return HPE_PAUSED;
}


}
}   //kit_muduo
