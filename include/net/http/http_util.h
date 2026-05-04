/**
 * @file http_util.h
 * @brief HTTP公共部分
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-30 15:14:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_UTIL_H__
#define __KIT_HTTP_UTIL_H__
#include "net/buffer.h"
#include "base/content_parser.h"

#include <bits/stdint-intn.h>
#include <string>
#include <assert.h>
#include <memory>
#include <iostream>
#include <vector>


namespace kit_muduo::http {

struct Version
{
    enum { kUnknow, kHttp10, kHttp11, kRtsp10};


    explicit Version(int32_t version = kUnknow): m_version(version) { }

    int32_t operator()() const { return m_version; }

    void set(int32_t val) { m_version = val; }

    const char* toString() const
    {
        switch (m_version)
        {
            case kHttp10: return "HTTP/1.0";
            case kHttp11: return "HTTP/1.1";
            case kRtsp10: return "RTSP/1.0";
            
            default:
                return "";
        }
        return "";
    }

    int32_t toInt() const { return m_version; }

    static Version FromString(const std::string &versionStr)
    {
        if("HTTP/1.0" == versionStr) return Version(kHttp10);
        if("HTTP/1.1" == versionStr) return Version(kHttp11);
        if("RTSP/1.0" == versionStr) return Version(kRtsp10);

        return Version();
    }

private:
    int32_t m_version{kUnknow};
};

struct ContentType
{
    enum {kUnknowType, kJsonType, kXmlType, kPlainType, kImageJpgType, kMultiForm, kOctetStream, kHtml, kCss, kJavaScript, kMax};

    explicit ContentType(int32_t contentType = kUnknowType): m_content_type(contentType) { }
    int32_t operator()() const { return m_content_type; }

    int32_t toInt() const { return m_content_type; }

    void set(int32_t val) { m_content_type = val; }

    const char * toString() const
    {
        switch (m_content_type)
        {
            case kJsonType: return "application/json";
            case kXmlType: return "application/xml";
            case kPlainType: return "text/plain";
            case kImageJpgType: return "image/jpeg";
            case kMultiForm: return "multipart/form-data";
            case kOctetStream: return "application/octet-stream";
            case kHtml: return "text/html";
            case kCss: return "text/css";
            case kJavaScript: return "text/javascript";

            default: return "application/json";
        }
        return "application/json";
    }

    // TODO 接口名称改一下
    // const char * toStr() const { return toString().c_str(); }

    static ContentType FromString(const std::string &contentTypeStr)
    {
        if(-1 != contentTypeStr.find("json"))
            return ContentType(kJsonType);
        if(-1 != contentTypeStr.find("xml"))
            return ContentType(kXmlType);
        if(-1 != contentTypeStr.find("plain"))
            return ContentType(kPlainType);
        if(-1 != contentTypeStr.find("jpeg"))
            return ContentType(kImageJpgType);
        if(-1 != contentTypeStr.find("multipart"))
            return ContentType(kMultiForm);
        if(-1 != contentTypeStr.find("stream"))
            return ContentType(kOctetStream);

        return ContentType(kUnknowType);
    }

    bool operator==(const ContentType &rc) const { return m_content_type == rc.m_content_type; }

private:
    int32_t m_content_type{kUnknowType};
};

/**
 * @brief 响应状态码
 */
struct StateCode
{
    enum
    {
        kUnknow = 0,
        //2XX
        k200Ok = 200,
        k204NoContent = 204, 
        //3XX
        k301MovedPermanently = 301,
        k302MoveTemporarily = 302,
        //4XX
        k400BadRequest = 400,
        k403Forbidden = 403,
        k404NotFound = 404,
        k405MethodNotAllowed = 405,
        k454SessionNotFound = 454,
        k455MethodNotValid = 455,
        //5XX
        k500InternalServerError = 500,
        k503ServiceUnavailable = 503,
        kMax,
    };

    explicit StateCode(int32_t code = kUnknow): m_code(code) { };
    ~StateCode() = default;

    int32_t operator()() const { return m_code; }

    void set(int32_t val) 
    {
        m_code = val; 
    }

    std::string toString() const
    {
        return m_code >= 100 && m_code < 600 ? std::to_string(m_code) : "";
    }
    
    int32_t toInt() const { return m_code; }

    static StateCode FromString(const std::string &str)
    {
        int32_t code = std::stoi(str);
        auto it = s_m_codeMessageMap.find(code);

        return it == s_m_codeMessageMap.end() ? StateCode() : StateCode(code);
    }

    std::string message() const
    {
        return s_m_codeMessageMap[m_code];
    }
private:
    static std::unordered_map<int32_t, std::string> s_m_codeMessageMap;

private:
    int32_t m_code;
    std::string m_message;
};


/**
 * @brief HTTP协议Body结构
 */
class Body
{
public:
    explicit Body(ContentType contentType = ContentType(ContentType::kPlainType))
        :_contentType(contentType)
    {}

    explicit Body(ContentType contentType, const std::vector<char>& data)
        :_contentType(contentType)
        ,_data(data)
    {}
    
    ~Body() = default;

    ContentType contentType() const { return _contentType; }

    void setContentType(int32_t contentTypeVal)
    {
        _contentType.set(contentTypeVal);
        // _contentParser = ContentParser::Creator(contentTypeVal);
    }

    void setContentType(const ContentType &contentType)
    {
        _contentType = contentType;
        // _contentParser = ContentParser::Creator(contentType());
    }

    void appendData(const std::string &data)
    {
        // 注意: 考虑一下string末尾的 /0
        appendData(data.data(), data.size());
    }


    void appendData(Buffer& buffer)
    {
        appendData(buffer.resetAllAsString());
    }
    
    void appendData(const char *start, size_t len)
    {
        _data.insert(_data.end(), start, start + len);
    }

    void appendData(const std::vector<char> & data)
    {
        _data.insert(_data.end(), data.begin(), data.end());
    }

    void reset() { _data.clear(); }

    std::vector<char> data() const { return _data; }
    std::string toString() const
    {
        return std::string(_data.begin(), _data.end());
    }

    // std::shared_ptr<ContentParser> contentParser() const { return _contentParser; }
    // void setContentParser(std::shared_ptr<ContentParser> parser) { _contentParser = std::move(parser); }

    // bool parse()
    // {
    //     if(_contentParser)
    //     {
    //         bool ok = _contentParser->parse(_data);
    //         _data = ok ? _contentParser->toString() : _data;
    //         return ok;
    //     }
    //     // 解析器不存在默认不需要解析
    //     return true;
    // }

private:
    /// @brief Body格式
    ContentType _contentType;
    /// @brief Body原始数据
    std::vector<char> _data;
    /// @brief Body解析器(内含格式化后的数据)
    // std::shared_ptr<ContentParser> _contentParser;
};

}

#endif
