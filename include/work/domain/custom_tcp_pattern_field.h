/**
 * @file custom_tcp_pattern_field.h
 * @brief 自定义TCP协议字段
 * @author ljk5
 * @version 1.0
 * @date 2025-11-11 14:48:49
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_CUSTOM_TCP_PATTERN_FIELD_H__
#define __KIT_CUSTOM_TCP_PATTERN_FIELD_H__

#include <string> 
#include <iostream>
#include <vector>

#include "nlohmann/json.hpp"
#include "net/net_data_converter.h"
#include "web/web_log.h"

namespace kit_domain {

template<class T>
class CustomTcpPatternField;

class CustomTcpPatternFieldBase
{
public:
    using Ptr = std::shared_ptr<CustomTcpPatternFieldBase>;
    using UPtr = std::unique_ptr<CustomTcpPatternFieldBase>;


    virtual ~CustomTcpPatternFieldBase() = default;

    virtual std::string toHexString(bool is_endian = false) = 0;

    virtual bool fromHexString(const std::string& hex_str) = 0;

    virtual std::vector<uint8_t> toBytes(bool is_endian = false) = 0;

    virtual bool fromBytes(const std::vector<uint8_t>& bytes, bool is_endian = false) = 0;

    virtual std::shared_ptr<CustomTcpPatternFieldBase> clone() = 0;

    virtual bool operator==(const std::shared_ptr<CustomTcpPatternFieldBase>& other) const = 0;

    virtual std::shared_ptr<CustomTcpPatternFieldBase> extract(const std::vector<char> &data, bool is_endian = false) = 0;


    friend std::ostream& operator<<(std::ostream &os, std::shared_ptr<CustomTcpPatternFieldBase> field)
    {
        assert(field);
        os << "\t" << field->name_ << "\n"
        << "\t" << field->idx_ << "\n"
        << "\t" << field->byte_pos_ << "\n"
        << "\t" << field->byte_len_ << "\n"
        << "\t" << field->type_.toString() << "\n"
        << "\t" << field->toHexString() << "\n";
        return os;
    }

    void setBytes(const std::vector<uint8_t> &bytes)
    {
        bytes_ = bytes;
        // 临时兼容: ValueToBytes(size_t/int64_t) 目前可能生成大于字段 byte_len_ 的字节数。
        // 在 net_data_converter 正式按字段宽度重构前，字段层只保留低位 byte_len_ 字节，
        // 避免 4 字节长度字段在响应序列化时膨胀成 8 字节。
        if(byte_len_ > 0 && bytes_.size() > static_cast<size_t>(byte_len_))
        {
            bytes_.erase(bytes_.begin(), bytes_.end() - byte_len_);
        }
    }

    void setBytes(const void* data, int32_t len) { bytes_.assign((char*)data, (char*)data + len); }

    void setTypeEnum(const kit_muduo::NetDataType &type) { type_ = type; }

    kit_muduo::NetDataType getTypeEnum() const { return type_; }

        // 通过抽象类设置真值（类型安全的模板方法）
    template<typename T>
    void setVal(const T& value) 
    {
        // if(sizeof(T) != byte_len_)
        // {
        //     throw std::runtime_error("type len not match!");
        // }
        // 手动截断?? 
        const auto &temp_bytes = std::move(kit_muduo::ValueToBytes(value));
        bytes_.assign(temp_bytes.begin() + temp_bytes.size() - sizeof(T)*2, temp_bytes.end());
    }
    
    // 通过抽象类获取真值（类型安全的模板方法）
    template<typename T>
    auto getVal() const {
        auto* derived = dynamic_cast<const CustomTcpPatternField<T>*>(this);
        if (!derived) 
        {
            throw std::runtime_error("Type mismatch in getVal");
        }
        return derived->getVal();
    }

    template<class T>
    void operator=(const T& value)
    {
        setVal(value);
        return;
    }

    template<class T>
    operator T() const 
    {
        return getVal<T>();
    }

    std::string name() const { return name_; }

    int32_t idx() const { return idx_; }
    int32_t byte_pos() const { return byte_pos_; }

    int32_t byte_len() const { return byte_len_; }

    void setSepcial(bool f) { is_special_ = f; }

    bool is_special() const { return is_special_; }

protected:
    /// @brief 字段名称
    std::string name_;
    /// @brief 字段在列表中的下标位置
    int32_t idx_;
    /// @brief 字段在实际数据流中起始byte位置
    int32_t byte_pos_;
    /// @brief 字段长度
    int32_t byte_len_;
    /// @brief 字段类型
    kit_muduo::NetDataType type_;
    /// @brief 是否是特殊字段 默认是
    bool is_special_{true};
    /// @brief 字段对应的字节流
    std::vector<uint8_t> bytes_;
};



template<class T>
class CustomTcpPatternField : public CustomTcpPatternFieldBase, public std::enable_shared_from_this<CustomTcpPatternField<T>>
{
public:

    ~CustomTcpPatternField() override = default;


    std::string toHexString(bool is_endian = false) override
    {
        
        try {
            
            // std::string hex_str = kit_muduo::DataToHexConverter<T>()(value_, is_endian);
            std::vector<uint8_t> tmp(bytes_);
            if(is_endian)
            {
                std::reverse(tmp.begin(), tmp.end());
            }
            std::string hex_str = kit_muduo::BytesToHexString(tmp, "");
            return hex_str;
        }catch(const std::exception& e) {
            KIT_ERROR(KIT_LOGGER("web"), "custom") << "toHexString failed: " << e.what() << std::endl;
            return "";
        }

    }

    bool fromHexString(const std::string& hex_str) override
    {

        try {
            // 十六进制字符串默认都是大端
            // value_ =  kit_muduo::HexToDataConverter<T>()(hex_str, false);
            bytes_ = kit_muduo::HexStringToBytes(hex_str);

        }catch(const std::exception& e) {
            KIT_ERROR(KIT_LOGGER("web"), "custom") << "fromHexString failed: " << e.what() << std::endl;

            return false;
        }

        return true;
    }

    std::vector<uint8_t> toBytes(bool is_endian = false) override
    {
        
        try {
            // T tmp = is_endian ? kit_muduo::SwapToBigEndian<T>(value_) : value_;
            // bytes = kit_muduo::ValueToBytes<T>(value_);
            std::vector<uint8_t> bytes(bytes_);
            if(is_endian)
            {
                std::reverse(bytes.begin(), bytes.end());
            }
            return bytes;
            
        }catch(const std::exception& e) {
            KIT_ERROR(KIT_LOGGER("web"), "custom") << "toBytes failed: " << e.what() << std::endl;

            return {};
        }

    }

    bool fromBytes(const std::vector<uint8_t>& bytes, bool is_endian = false) override
    {
        try {
            // value_ = kit_muduo::BytesToValue<T>(bytes, is_endian);
            bytes_ = bytes; // 拷贝一份
            if(is_endian)
            {
                std::reverse(bytes_.begin(), bytes_.end());
            }
            return true;

        }catch(const std::exception& e) {
            KIT_ERROR(KIT_LOGGER("web"), "custom") << "fromBytes failed: " << e.what() << std::endl;

            return false;
        }


    }

    std::shared_ptr<CustomTcpPatternFieldBase> clone() override
    {
        return std::make_shared<CustomTcpPatternField<T>>(*this);
    }

    bool operator==(const std::shared_ptr<CustomTcpPatternFieldBase>& other) const override
    {
        if(other == nullptr) 
            return false;
        auto o = std::dynamic_pointer_cast<CustomTcpPatternField<T>>(other);
        
        return o ? false : bytes_== o->bytes_;
    }

    bool operator==(const CustomTcpPatternField<T>& other) const
    {
        return this->bytes_ == other.bytes_;
    }

    std::shared_ptr<CustomTcpPatternFieldBase> extract(const std::vector<char> &data, bool is_endian = false) override
    {
        int32_t begin_pos = byte_pos_;
        int32_t end_pos = byte_pos_ + byte_len_;

        if(begin_pos < 0 || begin_pos >= data.size()
            || end_pos < 0 || end_pos > data.size() 
            || begin_pos > end_pos)
        {
            CUSTOM_ERROR() << "bytes out of range!" << std::endl;
            return nullptr;
        }

        // copy一份数据
        auto real_field = this->clone();
        std::vector<uint8_t> bytes(data.begin() + begin_pos, data.begin() + end_pos);

        // 字节顺序转换 不分大小端
        if(is_endian)
        {
            std::reverse(bytes.begin(), bytes.end());
        }

        // 根据起始位置 + 长度先读出 bytes数组
        real_field->setBytes(bytes);

        return real_field;
    }

    const T getVal() const 
    { 
        return value_; 
    }

    void setVal(const T& value) 
    {
        value_ = value;
    }

public:
    friend void to_json(nlohmann::json& root, const CustomTcpPatternField<T>& c) 
    {

        root["name"] = c.name_;
        root["idx"] = c.idx_;
        root["byte_pos"] = c.byte_pos_;
        root["byte_len"] = c.byte_len_;
        root["type"] = c.type_.toString();
        root["value"] = c.toHexString();
    } 

    friend void from_json(const nlohmann::json& root, CustomTcpPatternField<T>& c) 
    {
        c.name_ = root["name"];
        c.idx_ = root["idx"];
        c.byte_pos_ = root["byte_pos"];
        c.byte_len_ = root["byte_len"];
        c.type_ = kit_muduo::NetDataType::FromString(root["type"].get<std::string>());

        c.fromHexString(root["value"]);

    }

private:
    T value_;  // 转化后的实际数据
};



class CustomTcpPatternFieldFactory
{
public:
    static std::shared_ptr<CustomTcpPatternFieldBase> Create(const nlohmann::json &root)
    {
        const kit_muduo::NetDataType& field_type = kit_muduo::NetDataType::FromString(root.value("type", "UNDEF"));


        switch(field_type.get())
        {
        #define XX(type) \
            case kit_muduo::NetDataType::E::type: {auto c = std::make_shared<CustomTcpPatternField<kit_muduo::NetDataType::type>>(); *c = root; return c;}
    
            XX(INT8)
            XX(UINT8)
            XX(INT16)
            XX(UINT16)
            XX(INT32)
            XX(UINT32)
            XX(INT64)
            XX(UINT64)
            XX(FLOAT)
            XX(DOUBLE)
            XX(STR)
        #undef XX
            default: return nullptr;
        }
    }

    static std::shared_ptr<CustomTcpPatternFieldBase> Create(const kit_muduo::NetDataType::E type);

    static std::shared_ptr<CustomTcpPatternFieldBase> Create(const std::string& data);
};





}
#endif //__KIT_CUSTOM_TCP_PATTERN_FIELD_H__
