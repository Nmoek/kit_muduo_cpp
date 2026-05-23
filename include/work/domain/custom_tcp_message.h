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

#include <memory>
#include <unordered_map>
#include <vector>

#include "base/time_stamp.h"
#include "domain/custom_tcp_field_model.h"
#include "net/http/http_util.h"
#include "nlohmann/json.hpp"

namespace kit_domain {

class CustomTcpContext;

class CustomTcpMessage
{
public:

    /// byte_pos <---> FieldValue
    using HeadersValueMap = std::unordered_map<size_t, FieldValue>;

    CustomTcpMessage();

    ~CustomTcpMessage();


    CustomTcpMessage(CustomTcpMessage&&) = default;


    CustomTcpMessage& operator=(CustomTcpMessage&&) = default;

    void setRecordTime(kit_muduo::TimeStamp record_time) { recordTime_ = record_time; }
    kit_muduo::TimeStamp recordTime() const { return recordTime_; }
    kit_muduo::TimeStamp recordTime() { return recordTime_; }

    void setFunctionCodeHex(const std::string& value) { function_code_hex_ = value; }

    
    std::string functionCodeHex() const { return function_code_hex_; }

    void addField(const FieldValue &field_value);
    const FieldValue* getField(size_t byte_pos) const;

    const HeadersValueMap& headerFields() const { return header_fields_by_byte_pos_; }
    size_t getFieldNums() const { return header_fields_by_byte_pos_.size(); }

    /******暂时这么写**** */
    void setBody(const kit_muduo::http::Body& body) { body_ = body; }

    kit_muduo::http::Body& body() { return body_; }
    /******暂时这么写**** */

    /**
     * @brief 获取头部的总长度
     * @return int64_t 
     */
    uint64_t getHeaderBytes() const;



private:


    /// @brief 功能码十六进制表示值
    std::string function_code_hex_;
    /// @brief 按 byte_pos 索引的报文头字段表
    HeadersValueMap header_fields_by_byte_pos_;
    /// @brief 报文Body数据 先复用HTTP结构Body 后续更改该数据结构位置
    kit_muduo::http::Body body_;
    /// @brief 收发时间点
    kit_muduo::TimeStamp recordTime_;

};




}
#endif  //__KIT_CUSTOM_REQUEST_H__
