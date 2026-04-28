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
    CustomTcpMessage();

    CustomTcpMessage(int32_t header_field_num);
    
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
            header_fileds_.emplace_back(h ? h->clone(): nullptr);
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
            header_fileds_.emplace_back(h ? h->clone(): nullptr);
        }
        return *this;
    }

    CustomTcpMessage& operator=(CustomTcpMessage&&) = default;


    void setRecordTime(kit_muduo::TimeStamp record_time) { recordTime_ = record_time; }
    kit_muduo::TimeStamp recordTime() const { return recordTime_; }
    kit_muduo::TimeStamp recordTime() { return recordTime_; }

    void setFunctionCodeFieldValue(const std::string& value) { function_code_filed_value_ = value; }

    
    std::string functionCodeFieldValue() const { return function_code_filed_value_; }

    void addField(int32_t idx, std::shared_ptr<CustomTcpPatternFieldBase> field);

    std::shared_ptr<CustomTcpPatternFieldBase> getField(int32_t idx) const;

    void setFieldNums(size_t nums) {
        header_fileds_.resize(nums);
    }

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


public:
    friend void from_json(const nlohmann::json& j, CustomTcpMessage& message);

private:
    using HeaderVec =  std::vector<std::shared_ptr<CustomTcpPatternFieldBase>>;

    /// @brief 功能码十六进制表示值
    std::string function_code_filed_value_;
    /// @brief 报文头字段列表
    // 分两种情况: 
    // 解析请求 socket数据 --> header_data_
    // 组装报文 配置数据header_data_ --> socket数据
    HeaderVec header_fileds_;
    /// @brief 报文Body数据 先复用HTTP结构Body 后续更改该数据结构位置
    kit_muduo::http::Body body_;
    /// @brief 收发时间点
    kit_muduo::TimeStamp recordTime_;

};




}
#endif  //__KIT_CUSTOM_REQUEST_H__