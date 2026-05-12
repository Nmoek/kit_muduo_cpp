/**
 * @file custom_message.h
 * @brief 自定义TCP消息(请求/响应一体)
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 16:38:59
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_CUSTOM_REQUEST_H__
#define __KIT_CUSTOM_REQUEST_H__

#include <set>
#include <vector>
#include <memory>

#include "base/time_stamp.h"
#include "net/http/http_util.h"
#include "nlohmann/json.hpp"
#include "domain/custom_tcp_pattern_field.h"


namespace kit_domain {

class CustomTcpPattern;
class CustomTcpPatternFieldBase;
class CustomTcpContext;


class CustomTcpMessage
{
public:

    struct Compare
    {
        using is_transparent = std::true_type;

        bool operator()(std::shared_ptr<CustomTcpPatternFieldBase> a, std::shared_ptr<CustomTcpPatternFieldBase> b) const
        {
            // 字段起始字节位置 小->大
            return a->byte_pos() < b->byte_pos();
        }

        // 支持异构查找
        bool operator()(int32_t a, std::shared_ptr<CustomTcpPatternFieldBase> b) const
        {
            return a < b->byte_pos();
        }
        // 支持异构查找
        bool operator()(std::shared_ptr<CustomTcpPatternFieldBase> a, int32_t b) const
        {
            return a->byte_pos() < b;
        }
    };
    using HeadersSet = std::set<std::shared_ptr<CustomTcpPatternFieldBase>, Compare>;

    CustomTcpMessage();

    ~CustomTcpMessage();


    // 注意: 需要深拷贝
    CustomTcpMessage(const CustomTcpMessage& c)
    {
        body_ = c.body_;
        recordTime_ = c.recordTime_;
        function_code_filed_value_ = c.function_code_filed_value_;
        // 每个字段依次拷贝
        for(auto &h : c.header_fileds_)
        {
            header_fileds_.emplace(h ? h->clone(): nullptr);
        }
    }

    CustomTcpMessage(CustomTcpMessage&&) = default;

    CustomTcpMessage& operator=(const CustomTcpMessage& c)
    {
        if(this == &c) return *this; // 注意自赋值

        body_ = c.body_;
        recordTime_ = c.recordTime_;
        function_code_filed_value_ = c.function_code_filed_value_;
        // 每个字段依次拷贝
        for(auto &h : c.header_fileds_)
        {
            header_fileds_.emplace(h ? h->clone(): nullptr);
        }
        return *this;
    }

    CustomTcpMessage& operator=(CustomTcpMessage&&) = default;

    void setRecordTime(kit_muduo::TimeStamp record_time) { recordTime_ = record_time; }
    kit_muduo::TimeStamp recordTime() const { return recordTime_; }
    kit_muduo::TimeStamp recordTime() { return recordTime_; }

    void setFunctionCodeFieldValue(const std::string& value) { function_code_filed_value_ = value; }

    
    std::string functionCodeFieldValue() const { return function_code_filed_value_; }

    void addField(std::shared_ptr<CustomTcpPatternFieldBase> field);

    std::shared_ptr<CustomTcpPatternFieldBase> getField(int32_t idx) const;


    const HeadersSet& headerFields() const { return header_fileds_; }
    size_t getFieldNums() const { return header_fileds_.size(); }

    /******暂时这么写**** */
    void setBody(const kit_muduo::http::Body& body) { body_ = body; }

    kit_muduo::http::Body& body() { return body_; }
    /******暂时这么写**** */

    /**
     * @brief 获取头部的总长度
     * @return int64_t 
     */
    int64_t getHeaderBytes() const;

    /**
     * @brief 全部头部字段组装为二进制
     * @param is_endian 
     * @return std::vector<uint8_t> 
     */
    std::vector<uint8_t> assembleHeaders(bool is_endian = false) const;

public:
    friend void from_json(const nlohmann::json& j, CustomTcpMessage& message);

private:


    /// @brief 功能码十六进制表示值
    std::string function_code_filed_value_;
    /// @brief 报文头字段列表
    // 分两种情况: 
    // 解析请求 socket数据 --> header_data_
    // 组装报文 配置数据header_data_ --> socket数据
    HeadersSet header_fileds_;
    /// @brief 报文Body数据 先复用HTTP结构Body 后续更改该数据结构位置
    kit_muduo::http::Body body_;
    /// @brief 收发时间点
    kit_muduo::TimeStamp recordTime_;

};




}
#endif  //__KIT_CUSTOM_REQUEST_H__