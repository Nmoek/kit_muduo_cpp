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
#include <string>
#include <unordered_map>
#include <vector>

#include "base/time_stamp.h"
#include "domain/custom_tcp_field_model.h"

namespace kit_domain {

class CustomTcpContext;
class CustomTcpPattern;

class CustomTcpMessage
{
public:

    /// byte_pos <---> FieldValue
    using HeadersValueMap = std::map<size_t, FieldValue>;

    explicit CustomTcpMessage(std::shared_ptr<CustomTcpPattern> pattern);

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

    const std::vector<uint8_t>& bodyData() const { return body_data_; }
    std::vector<uint8_t>& bodyData() { return body_data_; }
    void setBodyData(const std::vector<char> &data) { body_data_.assign(data.begin(), data.end()); }
    void setBodyData(const std::vector<uint8_t>& data) { body_data_ = data; }
    void setBodyData(std::vector<uint8_t>&& data) { body_data_ = std::move(data); }
    void appendBodyData(const char* start, size_t len);
    void appendBodyData(const std::vector<uint8_t>& data);
 
    /**
     * @brief 获取头部的总长度
     * @return uint64_t 
     */
    uint64_t getHeaderLen() const;

    std::string toHeaderString() const;
    std::optional<std::vector<uint8_t>> toBytes() const;
    std::string toString() const;

private:
    /// @brief 功能码十六进制表示值
    std::string function_code_hex_;
    /// @brief 按 byte_pos 索引的报文头字段表
    HeadersValueMap header_fields_by_byte_pos_;
    /// @brief 报文Body原始字节
    std::vector<uint8_t> body_data_;
    /// @brief 收发时间点
    kit_muduo::TimeStamp recordTime_;
    /// @brief 当前受控的格式弱指针
    std::weak_ptr<CustomTcpPattern> weak_pattern_;
};
using CustomTcpMessagePtr = std::shared_ptr<CustomTcpMessage>;



}
#endif  //__KIT_CUSTOM_REQUEST_H__
