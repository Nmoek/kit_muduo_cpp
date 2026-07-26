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

struct HttpParseLimits
{
    size_t max_start_line_bytes{8 * 1024};
    /// @brief  Header 字段名和值的总字节数，不包含冒号、空白和 CRLF。
    size_t max_header_bytes{32 * 1024};
    size_t max_header_count{100};
    size_t max_body_bytes{16 * 1024 * 1024};
    size_t max_error_capture_bytes{1 * 1024};
};

enum class HttpParseError
{
    kNone,
    kInvalidFormat,
    kStartLineTooLarge, // 414 URI Too Long
    kHeadersTooLarge, // 431 Request Header Fields Too Large
    kHeadersTooMany, // 431 Request Header Fields Too Large
    kBodyTooLarge, // 413 Payload Too Large
    kUnsupportedTransferEncoding,
};

class HttpParser
{
public:
    enum Type {
        UnkownType    = 0,
        ReqType       = 1,
        RespType      = 2,
    };
    HttpParser(HttpContext *context, HttpParseLimits limits = {})
        :context_(context)
        ,type_(ReqType)
        ,limits_(limits)
    {}

    virtual bool parse(Buffer &buf) = 0;
    virtual bool parse(const std::string &data) = 0;
    void setType(int32_t type) { type_= type; }

    const HttpParseLimits& limits() const { return limits_; }

public:
    static inline bool TryConsume(size_t& cur_bytes, size_t in_bytes, size_t limit_bytes)
    {
        if(cur_bytes > limit_bytes || in_bytes > limit_bytes - cur_bytes)
        {
            return false;
        }
        cur_bytes += in_bytes;
        return true;
    }

protected:
    HttpContext *context_;
    /// @brief 解析模式 指示给哪个变量赋值
    int32_t type_;
    /// @brief 报文解析限制(TODO 暂时作为默认 不可配置)
    HttpParseLimits limits_;
    /// @brief 累积首行长度
    size_t start_line_bytes_{0};
    /// @brief 累积头部字段长度
    size_t header_bytes_{0};
    /// @brief 累积头部字段个数
    size_t header_count_{0};
    /// @brief 累积Body长度
    size_t body_bytes_{0};
};

/**
 * @brief 自定义解析
 */
class CustomHttpParser: public HttpParser
{
public:
    CustomHttpParser(HttpContext *context, HttpParseLimits limits = {})
        :HttpParser(context, limits)
    { }

    bool parse(Buffer &buf) override;
    bool parse(const std::string &data) override;
private:
    bool processRequestLine(const char *start, const char *end);

    bool fail(HttpParseError error);

    bool parseContentLength(const std::string& value, size_t& content_length);

    void captureParseError(Buffer& buf);

    const char* findCRLF(Buffer &buf) const;

    const char* findCRLF(const char *start, const char *end) const;
private:
    size_t expected_body_len_{0};
    size_t read_len_{0};
    size_t pending_header_bytes_{0}; // 防止攻击单条header过长
    bool has_content_length_{false};
};

/**
 * @brief llhttp库解析
 */
class LLhttpParser: public HttpParser
{
public:
    struct HeaderContext {
        std::string cur_header;
        std::string cur_header_val;
        std::string url;
        std::unordered_map<std::string, std::string> headers;
    };

    LLhttpParser(HttpContext *context);

    bool parse(Buffer &buf) override;

    bool parse(const std::string &data) override;

private:
    static int onMessageBegin(llhttp_t* parser);

    static int onMethod(llhttp_t* parser, const char *data, size_t len);

    static int onStatus(llhttp_t* parser, const char *data, size_t len);

    static int onStatusComplete(llhttp_t* parser);

    static int onUrl(llhttp_t* parser, const char *data, size_t len);

    static int onUrlComplete(llhttp_t* parser);

    static int onVersion(llhttp_t* parser, const char *data, size_t len);

    static int onVersionComplete(llhttp_t* parser);

    static int onHeaderField(llhttp_t* parser, const char *data, size_t len);
    static int onHeaderFieldComplete(llhttp_t* parser);


    static int onHeaderValue(llhttp_t* parser, const char *data, size_t len);
    static int onHeaderValueComplete(llhttp_t* parser);

    static int onHeadersComplete(llhttp_t* parser);

    static int onBody(llhttp_t* parser, const char *data, size_t len);
    // 该回调函数必须设置
    static int onMessageComplete(llhttp_t* parser);


    void parseQueryParams(const std::string &query, const HttpRequestPtr &request);


    void parseUrl(const std::string &url, const HttpRequestPtr &request);

    void clear();

    int32_t fail(llhttp_t *parser, HttpParseError error, const char* reason);

private:
    /// @brief llhttp库句柄
    llhttp_t parser_;
    /// @brief llhttp库配置
    llhttp_settings_t settings_;
    /// @brief 头部字段解析
    HeaderContext head_ctx_;
    /// @brief 是否是暂停状态
    bool is_paused_;
};


}
}   //kit_muduo

#endif
