/**
 * @file custom_tcp_pattern.h
 * @brief 自定义TCP协议格式
 * @author ljk5
 * @version 1.0
 * @date 2025-10-29 15:47:52
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_CUSTOM_TCP_PATTERN_H__
#define __KIT_CUSTOM_TCP_PATTERN_H__

#include "nlohmann/json.hpp"
#include "domain/type.h"
#include "domain/custom_tcp_pattern_field.h"

#include <string>
#include <memory>
#include <vector>

namespace kit_domain {

class CustomTcpMessage;

class CustomTcpPattern
{
public:

    virtual ~CustomTcpPattern() = default;

    virtual CustomTcpPatternType getPatternType() const = 0;

    /**
     * @brief 长度信息字段
     * @return std::shared_ptr<CustomTcpPatternFieldBase> 
     */
    virtual std::shared_ptr<CustomTcpPatternFieldBase> lengthInfoField() const = 0;

    /**
     * @brief 获取所有特殊字段
     * @return std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> 
     */
    virtual std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> getSpecialFields() const = 0;

    /**
     * @brief 计算剩余bytes长度大小
     * @return int64_t 
     */
    virtual int64_t calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field) = 0;

    /**
     * @brief 将报文按特定格式进行序列化
     * @param message 
     * @param is_endian 
     * @return std::vector<char> 
     */
    virtual std::vector<char> serialize(std::shared_ptr<CustomTcpMessage> message, bool is_endian) = 0;

    /**
     * @brief 获取配置的协议头bytes长度
     * @return int32_t 
     */
    int32_t headByteLen() const { return least_byte_len_; }

        /**
     * @brief 获取配置的头部字段数量
     * @return int32_t 
     */
    // int32_t getHeadFieldNums() const { return head_filed_num_; };


    std::shared_ptr<CustomTcpPatternFieldBase> startMagicNumField() const  { return start_magic_num_field_; }

    std::shared_ptr<CustomTcpPatternFieldBase> functionCodeField() const  { return function_code_field_; }

    /**
     * @brief 提取数据并检查某些特殊字段
     * @param data 
     * @return std::shared_ptr<CustomTcpPatternFieldBase>  字段
     */
    std::shared_ptr<CustomTcpPatternFieldBase> checkField(std::shared_ptr<CustomTcpPatternFieldBase> field, const std::vector<char> &data);



protected:
    /// @brief 至少收取多少长度才开始解析
    int32_t least_byte_len_{0};
    /// @brief 头部字段数量
    int32_t head_filed_num_;
    /// @brief 起始魔数字字段
    std::shared_ptr<CustomTcpPatternFieldBase> start_magic_num_field_;
    /// @brief 功能码字段
    std::shared_ptr<CustomTcpPatternFieldBase> function_code_field_;

};


/// @brief BodyLengthDepPattern 头中有Body长度的格式
class BodyLengthDepPattern : public CustomTcpPattern 
{
public:

    BodyLengthDepPattern(const std::vector<char>& info);
    
    ~BodyLengthDepPattern() override = default;

    CustomTcpPatternType getPatternType() const override { return CustomTcpPatternType::BODY_LENGTH_DEP; };


    std::shared_ptr<CustomTcpPatternFieldBase> lengthInfoField() const override { return body_length_field_; }

    int64_t calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field) override;


    std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> getSpecialFields() const override;

    std::vector<char> serialize(std::shared_ptr<CustomTcpMessage> message, bool is_endian) override;

public:
    friend void to_json(nlohmann::json& root, const BodyLengthDepPattern& b);

    friend void from_json(const nlohmann::json& root, BodyLengthDepPattern& b);


private:

    std::shared_ptr<CustomTcpPatternFieldBase> body_length_field_;
};


/// @brief TotalLengthDepPattern 头中有报文总长度长度的格式
class TotalLengthDepPattern : public CustomTcpPattern 
{
public:


    TotalLengthDepPattern(const std::vector<char>& info);
    
    ~TotalLengthDepPattern() override = default;

    CustomTcpPatternType getPatternType() const override { return CustomTcpPatternType::TOTAL_LENGTH_DEP; };


    std::shared_ptr<CustomTcpPatternFieldBase> lengthInfoField() const override { return total_length_field_; }

    int64_t calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field) override;

    std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> getSpecialFields() const override;
    std::vector<char> serialize(std::shared_ptr<CustomTcpMessage> message, bool is_endian) override;
    

public:
    friend void to_json(nlohmann::json& root, const TotalLengthDepPattern& b);

    friend void from_json(const nlohmann::json& root, TotalLengthDepPattern& b);

private:
    /// @brief 报文总长度字段
    std::shared_ptr<CustomTcpPatternFieldBase> total_length_field_;
};

/// @brief NoLengthDepPattern 头中没有长度信息可依赖的格式
class NoLengthDepPattern : public CustomTcpPattern 
{
public:
    NoLengthDepPattern(const std::vector<char>& info);
    
            
    ~NoLengthDepPattern() override = default;

    CustomTcpPatternType getPatternType() const override { return CustomTcpPatternType::NO_LENGTH_DEP; };

    
    std::shared_ptr<CustomTcpPatternFieldBase> lengthInfoField() const override { return nullptr; }

    int64_t calRemainBytesLen(std::shared_ptr<CustomTcpPatternFieldBase> field) override;

    std::vector<std::shared_ptr<CustomTcpPatternFieldBase>> getSpecialFields() const override;

    std::vector<char> serialize(std::shared_ptr<CustomTcpMessage> message, bool is_endian) override;
    

public:
    friend void to_json(nlohmann::json& root, const NoLengthDepPattern& b);

    friend void from_json(const nlohmann::json& root, NoLengthDepPattern& b);

private:
    // 不好出处理 先不使用
    // std::shared_ptr<CustomTcpPatternFieldBase> end_magic_num_field_;
};


/// @brief CustomTcpPattern的简单工厂
class CustomTcpPatternFactory
{
public:

    static std::shared_ptr<CustomTcpPattern> Create(CustomTcpPatternType type, const std::vector<char>& info);

};

}
#endif //__KIT_DOMAIN_CUSTOM_TCP_PATTERN_H__