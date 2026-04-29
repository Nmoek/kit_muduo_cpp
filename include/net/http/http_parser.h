/**
 * @file http_parser.h
 * @brief HTTP报文解析器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 20:30:14
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_PARSER_H__
#define __KIT_HTTP_PARSER_H__

#include "net/call_backs.h"

#include <llhttp.h>
#include <string>
#include <unordered_map>

namespace kit_muduo {

class Buffer;
class ContentParser;

namespace http {

class HttpContext;

class HttpParser
{
public:
    enum Type {
        UnkownType    = 0,
        ReqType       = 1,
        RespType      = 2,
    };
    HttpParser(HttpContext *context)
        :_context(context)
        ,_type(ReqType)
    {}

    virtual bool parse(Buffer &buf) = 0;
    virtual bool parse(const std::string &data) = 0;
    void setType(int32_t type) { _type= type; }

protected:
    HttpContext *_context;
    /// @brief 解析模式 指示给哪个变量赋值
    int32_t _type;
};

/**
 * @brief 自定义解析
 */
class CustomHttpParser: public HttpParser
{
public:
    CustomHttpParser(HttpContext *context)
        :HttpParser(context)
    { }

    bool parse(Buffer &buf) override;
    bool parse(const std::string &data) override;
private:
    bool processRequestLine(const char *start, const char *end);

    const char* findCRLF(Buffer &buf) const;

    const char* findCRLF(const char *start, const char *end) const;
private:
    size_t expected_body_len_{0};
    size_t read_len_{0};
};

/**
 * @brief llhttp库解析
 */
class LLhttpParser: public HttpParser
{
public:
    struct HeaderContext {
        std::string cur_header;
        std::string url;
        std::unordered_map<std::string, std::string> headers;
    };

    LLhttpParser(HttpContext *context);

    bool parse(Buffer &buf) override;

    bool parse(const std::string &data) override;

private:
    static  int onMethod(llhttp_t* parser, const char *data, size_t len);

    static  int onStatus(llhttp_t* parser, const char *data, size_t len);

    static int onStatusComplete(llhttp_t* parser);

    static int onUrl(llhttp_t* parser, const char *data, size_t len);

    static int onUrlComplete(llhttp_t* parser);

    static int onVersion(llhttp_t* parser, const char *data, size_t len);

    static int onVersionComplete(llhttp_t* parser);

    static int onHeaderField(llhttp_t* parser, const char *data, size_t len);

    static int onHeaderValue(llhttp_t* parser, const char *data, size_t len);

    static int onHeadersComplete(llhttp_t* parser);

    static int onBody(llhttp_t* parser, const char *data, size_t len);
    // 该回调函数必须设置
    static int onMessageComplete(llhttp_t* parser);


    void parseQueryParams(const std::string &query, const HttpRequestPtr &request);


    void parseUrl(const std::string &url, const HttpRequestPtr &request);

private:
    /// @brief llhttp库句柄
    llhttp_t _parser;
    /// @brief llhttp库配置
    llhttp_settings_t _settings;
    /// @brief 头部字段解析
    HeaderContext _headerCtx;
};


}
}   //kit_muduo

#endif
