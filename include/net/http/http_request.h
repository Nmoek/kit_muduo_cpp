/**
 * @file http_request.h
 * @brief HTTP请求
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 20:19:34
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_HTTP_REQUEST_H__
#define __KIT_HTTP_REQUEST_H__
#include "base/time_stamp.h"
#include "net/http/http_content.h"
#include "net/http/http_util.h"

#include <string>
#include <unordered_map>
#include <assert.h>
#include <vector>



namespace kit_muduo::http {

class HttpRequest
{
public:
    struct Method
    {
        enum { kInvaild, kGet, kPost, kHead, kPut, kDelete };

        explicit Method(int32_t method = kInvaild) :method(method) { }

        int32_t operator()() const { return method; }

        void set(int32_t val) { method = val; }

        int32_t toInt() const { return method; }

        std::string toString() const { return toStr(); }
        
        const char* toStr() const
        {
            switch (method)
            {
                case kGet: return "GET";
                case kPost: return "POST";
                case kHead: return "HEAD";
                case kPut: return "PUT";
                case kDelete: return "DELETE";
                default:
                    return "Invaild";
            }
            return "Invaild";
        }

        static Method FromString(const std::string &methodStr)
        {
            if("GET" == methodStr) return Method(kGet);
            if("POST" == methodStr) return Method(kPost);
            if("HEAD" == methodStr) return Method(kHead);
            if("PUT" == methodStr) return Method(kPut);
            if("DELETE" == methodStr) return Method(kDelete);
            return Method();
        }
    private:
        int32_t method{kInvaild};
    };


    HttpRequest();
    HttpRequest(const HttpRequest&) = default;

    ~HttpRequest();


    Method method() const { return method_; }
    void setMethod(int32_t methodVal) { method_.set(methodVal); }
    void setMethod(Method method) { method_ = std::move(method); }


    std::string path() const { return path_; }
    void setPath(const std::string &path) { path_ = NormalizeHttpPath(path); }

    std::string getQureyParam(const std::string &key) const
    { 
        auto it = query_params_.find(key);
        return it ==  query_params_.end() ? "" : it->second;
    } 

    void addQureyParam(const std::string &key, const std::string &val) { query_params_[key] = val; }

    std::string getRouteParam(const std::string &key) const
    {
        auto it = route_params_.find(key);
        return it == route_params_.end() ? "" : it->second;
    }

    void addRouteParam(const std::string &key, const std::string &val) { route_params_[key] = val; }


    Version version() const { return version_; }
    void setVersion(int32_t versionVal) { version_.set(versionVal); }
    void setVersion(Version version) { version_ = std::move(version); }

    void addHeader(const std::string& head, const std::string &val);
    bool addHeader(const char *start, const char *colon, const char *end);
    std::string getHeader(const std::string &key) const;

    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }
    std::unordered_map<std::string, std::string>& headers() { return headers_; }
    void setHeaders(const std::unordered_map<std::string, std::string> &headers);


    void setReceiveTime(TimeStamp receiveTime) { receive_time_ = receiveTime; }
    TimeStamp receiveTime() const { return receive_time_; }
    TimeStamp receiveTime() { return receive_time_; }

    const ContentMeta& contentMeta() const { return content_meta_; }
    ContentMeta& contentMeta() { return content_meta_; }
    void setContentMeta(const ContentMeta& meta);
    void setContentMeta(ContentMeta&& meta);

    const std::vector<uint8_t>& bodyData() const { return body_data_; }
    std::vector<uint8_t>& bodyData() { return body_data_; }
    void setBodyData(const std::vector<uint8_t>& data) { body_data_ = data; }
    void setBodyData(std::vector<uint8_t>&& data) { body_data_ = std::move(data); }
    void setBodyData(const std::vector<char>& data);
    void setBodyData(const std::string& data);
    void appendBodyData(const char* start, size_t len);
    void appendBodyData(const std::string& data);
    void appendBodyData(const std::vector<char>& data);
    void appendBodyData(const std::vector<uint8_t>& data);
    void resetBodyData() { body_data_.clear(); }
    std::string bodyString() const;

    /**
     * @brief 报文序列化
     * @return std::string
     */
    std::vector<uint8_t> toBytes();
    std::string toString();

private:

    /// @brief 请求方法
    Method method_;
    
protected:
    using ParamMap = std::unordered_map<std::string, std::string>;
    /// @brief 请求路径
    std::string path_;
    /// @brief 请求参数
    ParamMap query_params_;
    /// @brief 动态路由参数
    ParamMap route_params_;
    /// @brief 协议版本
    Version version_;
    /// @brief 头部字段
    std::unordered_map<std::string, std::string> headers_;
    /// @brief Content-Type 元数据
    ContentMeta content_meta_;
    /// @brief Body原始字节
    std::vector<uint8_t> body_data_;
    /// @brief 接收请求时间点
    TimeStamp receive_time_;

};


}

#endif
