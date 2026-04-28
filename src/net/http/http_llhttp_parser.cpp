/**
 * @file http_parser.cpp
 * @brief HTTP报文解析器-llhttp库
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


#include <functional>

namespace kit_muduo {
namespace http {

LLhttpParser::LLhttpParser(HttpContext *context)
    :HttpParser(context)
{
    llhttp_settings_init(&_settings);
    _settings.on_method = &LLhttpParser::onMethod;
    _settings.on_status = &LLhttpParser::onStatus;
    _settings.on_status_complete = &LLhttpParser::onStatusComplete;
    _settings.on_url = &LLhttpParser::onUrl;
    _settings.on_version = &LLhttpParser::onVersion;
    _settings.on_header_field = &LLhttpParser::onHeaderField;
    _settings.on_header_value = &LLhttpParser::onHeaderValue;
    _settings.on_headers_complete = &LLhttpParser::onHeadersComplete;
    _settings.on_body = &LLhttpParser::onBody;
    _settings.on_message_complete = &LLhttpParser::onMessageComplete;

    llhttp_init(&_parser, HTTP_BOTH, &_settings);

    _parser.data = static_cast<void*>(this);

}

bool LLhttpParser::parse(const std::string &data)
{
    // 开启解析
    // 注意需要把\0去掉
    llhttp_errno err = llhttp_execute(&_parser, data.c_str(), data.size());
    if (err == HPE_OK)
    {
        HTTP_DEBUG() << "Successfully parsed!\n";
    }
    else
    {
        HTTP_ERROR() << "Parse error: " << llhttp_errno_name(err) << ", "
            << llhttp_get_error_reason(&_parser)
            << ", err pos:" << "`" << llhttp_get_error_pos(&_parser) << "`"
            << std::endl;

        return false;
    }
    return true;
}

bool LLhttpParser::parse(Buffer &buf)
{
    const std::string& data = buf.resetAllAsString();
    return parse(data);
}

int LLhttpParser::onMethod(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->_context->request();

    const std::string &s = std::string(data, len);
    HTTP_DEBUG() << "method: " << s << std::endl;
    
    request->setMethod(HttpRequest::Method::FromString(s));
    return 0;
}

int LLhttpParser::onStatus(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->_context->request();
    HttpResponsePtr response = parser_ptr->_context->response();

    HTTP_DEBUG() << "status: " << llhttp_get_status_code(parser) << std::endl;

    response->setStateCode(llhttp_get_status_code(parser));
    return 0;
}

int LLhttpParser::onStatusComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    HTTP_DEBUG() << "response line parse ok" << std::endl;

    // 状态转换
    if(RespType == parser_ptr->_type)
        parser_ptr->_context->setState(HttpContext::kExpectHeaders);
    
    return 0;
}


int LLhttpParser::onUrl(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->_context->request();

    const std::string &s = std::string(data, len);
    HTTP_DEBUG() << "url: " << s << std::endl;

    request->setPath(s);
    
    return 0;
}

int LLhttpParser::onVersion(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->_context->request();
    HttpResponsePtr response = parser_ptr->_context->response();

    std::string s = "HTTP/";
    s += std::string(data, len);
    HTTP_DEBUG() << "version: " << s << std::endl;

    if(ReqType == parser_ptr->_type)
        request->setVersion(Version::FromString(s));
    else
        response->setVersion(Version::FromString(s));
    return 0;
}


int LLhttpParser::onVersionComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);

    HTTP_DEBUG() << "request line parse ok" << std::endl;

    // 状态转换
    if(ReqType == parser_ptr->_type)
        parser_ptr->_context->setState(HttpContext::kExpectHeaders);
    
    return 0;
}

int LLhttpParser::onHeaderField(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HeaderContext &ctx = parser_ptr->_headerCtx;
    ctx.cur_header = std::string(data, len);
    return 0;
}

int LLhttpParser::onHeaderValue(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HeaderContext &ctx = parser_ptr->_headerCtx;

    if(!ctx.cur_header.empty())
    {
        ctx.headers[ctx.cur_header] += std::string(data, len);
    }
    return 0;
}

int LLhttpParser::onHeadersComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HeaderContext &ctx = parser_ptr->_headerCtx;
    HttpRequestPtr request = parser_ptr->_context->request();
    HttpResponsePtr response = parser_ptr->_context->response();

    for(auto &it : ctx.headers)
    {
        HTTP_DEBUG() << it.first << " : " << it.second << std::endl;
    }

    // headers字段赋值
    if(ReqType == parser_ptr->_type)
        request->setHeaders(ctx.headers);
    else
        response->setHeaders(ctx.headers);
    
        // 状态转换
    parser_ptr->_context->setState(HttpContext::kExpectBody);
    return 0;
}

int LLhttpParser::onBody(llhttp_t* parser, const char *data, size_t len)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->_context->request();
    HttpResponsePtr response = parser_ptr->_context->response();

    const std::string &s = std::string(data, len);
    int64_t content_len = atoi(request->getHeader("Content-Length").c_str());
    const std::string &content_type_str = parser_ptr->_type == ReqType ? request->getHeader("Content-Type") : response->getHeader("Content-Type");

    // TODO：需要分情况检查Content-Length是否应该被包含
    // 当前处理默认必须包含
    if(content_len <= 0 || content_type_str.empty())
    {
        HTTP_ERROR() << "Content-Length/Content-Type is invalid!" << std::endl;
        return -1;
    }

    const ContentType &content_type = ContentType::FromString(content_type_str);

    if(ReqType == parser_ptr->_type)
    {
        request->body().setContentType(content_type);
        request->body().appendData(s);
    }
    else 
    {
        response->body().setContentType(content_type);
        response->body().appendData(s);
    }

    HTTP_F_DEBUG("Body[%d]: %s \n", parser_ptr->_type, s.c_str());

    return 0;
}
// 该回调函数必须设置
int LLhttpParser::onMessageComplete(llhttp_t* parser)
{
    LLhttpParser* parser_ptr = static_cast<LLhttpParser*>(parser->data);
    HttpRequestPtr request = parser_ptr->_context->request();
    HttpResponsePtr response = parser_ptr->_context->response();
 
    HTTP_F_INFO("http request/response parse finish! body data size: [%lld/%lld]\n", (ReqType == parser_ptr->_type ? 
        request->body().data().size() : response->body().data().size()), 
        parser->content_length);
    
    // 头部上下文清除一下
    parser_ptr->_headerCtx = HeaderContext();
    // 解析完成
    parser_ptr->_context->setState(HttpContext::kGotAll);

    return 0;
}


}
}   //kit_muduo