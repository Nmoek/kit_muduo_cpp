/**
 * @file net_data_converter.h
 * @brief 二进制网络数据转换器
 * @author ljk5
 * @version 1.0
 * @date 2025-11-07 11:02:34
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_NET_DATA_CONVERTER_H__
#define __KIT_NET_DATA_CONVERTER_H__

#include <string>
#include <stdexcept>
#include <vector>
#include <cstring>

#include "net/endian.h"

#define KIT_HEX_PREFIX  ("H")

namespace kit_muduo {


struct NetDataType 
{
    enum class E {
        UNDEF = 0,
        INT8, UINT8,
        INT16, UINT16,
        INT32, UINT32,
        INT64, UINT64,
        FLOAT, DOUBLE,
        STR,
    };

    using INT8 = int8_t;
    using UINT8 = uint8_t;
    using INT16 = int16_t;
    using UINT16 = uint16_t;
    using INT32 = int32_t;
    using UINT32 = uint32_t;
    using INT64 = int64_t;
    using UINT64 = uint64_t;
    using FLOAT = float;
    using DOUBLE = double;
    using STR = std::string;

    NetDataType() = default;
    NetDataType(const NetDataType&) = default;
    NetDataType(NetDataType&&) = default;

    NetDataType& operator=(const NetDataType&) = default;
    NetDataType& operator=(NetDataType&&) = default;
    ~NetDataType() = default;

    NetDataType(NetDataType::E type)
        :m_type(type)
    { }

    NetDataType& set(NetDataType::E type) { m_type = type; return *this; }

    NetDataType::E get() const { return m_type; }

    static NetDataType FromString(const std::string &type_str);

    size_t getTypeSize();

    std::string toString() const { return toStrs(); }
    const char* toStrs() const;

    int32_t toInt() const { return static_cast<int32_t>(m_type); }

private:
    NetDataType::E m_type{NetDataType::E::UNDEF};
};

struct NetDataConfig {
    NetDataType m_type;
    size_t m_byteLength;
    bool m_isBigEndian;  // 是否按照大端字节序转换
};

// 十六进制字符转数值
uint8_t HexCharToValue(char c);

char ValueToHexChar(uint8_t v);


// 验证字符串长度是否匹配字节长度
bool CheckHexString(const std::string& hex_str, size_t byte_len);

// 十六进制字符串转字节数组(不进行大小端转换)
std::vector<uint8_t> HexStringToBytes(const std::string& hex_str);

// 字节数组转十六进制字符串(不进行大小端转换)
std::string BytesToHexString(const std::vector<uint8_t> bytes, const std::string& prefix = " ");

// 任意数据转字节数组(不进行大小端转换)
template<class T>
std::vector<uint8_t> ValueToBytes(T val, bool is_endian = false)
{
    union {
        T val;
        uint8_t bytes[sizeof(T)];
    }val_helper = {0};


    val_helper.val = val;
    std::vector<uint8_t> res(val_helper.bytes, val_helper.bytes + sizeof(T));
    if(is_endian)
    {
        std::reverse(res.begin(), res.end());
    }

    return res;
}

template<>
std::vector<uint8_t> ValueToBytes(std::string val, bool is_endian);


// 字节数组转为任意数据(不进行大小端转换)
template<class T>
T BytesToValue(const std::vector<uint8_t>& bytes, bool is_endian = false)
{
    union {
        T val;
        uint8_t bytes[sizeof(T)];
    }val_helper = {0};

    for(int i = 0;i < bytes.size();++i)
    {
        if(is_endian)
        {
            val_helper.bytes[i] = bytes[sizeof(T) - i - 1];
        }
        else
        {
            val_helper.bytes[i] = bytes[i];
        }
    }

    return val_helper.val;
}

template<>
std::string BytesToValue(const std::vector<uint8_t>& bytes, bool is_endian);

/*
    Hex Str --> T
    0x1234  --> int、long
    默认转为大端实际数据
*/
template<class T>
class HexToDataConverter
{
public:
    T operator()(const std::string& hex_str, bool is_endian = true)
    {
        // 将十六进制字符串转换为字节数组
        std::vector<uint8_t> bytes = HexStringToBytes(hex_str);

        if (bytes.size() != sizeof(T)) 
        {
            throw std::invalid_argument("Hex string length doesn't match byte length");
        }

        // 是否字节序转换
        return BytesToValue<T>(bytes, is_endian);
    }
};

// 特化 Hex Str --> string(ACII)
template<>
class HexToDataConverter<std::string>
{
public:
    std::string operator()(const std::string& hex_str, bool is_endian = true)
    {
        // ACII码转换 即可
        std::vector<uint8_t> bytes = HexStringToBytes(hex_str);
        return std::string(bytes.begin(), bytes.end());
    }
};

/**************************************** */

/*
    T --> Hex Str
    int、long --> "0x1234"
    默认转为大端展示
*/
template<class T>
class DataToHexConverter
{
public:
    std::string operator()(T val, bool is_endian = true)
    {
        std::vector<uint8_t> bytes = ValueToBytes<T>(val);

        if(is_endian)
        {
            std::reverse(bytes.begin(), bytes.end());
        }

        return BytesToHexString(bytes, "");
    }
};


// 特化 (ACII)string --> Hex Str
template<>
class DataToHexConverter<std::string>
{
public:
    std::string operator()(std::string &s, bool is_endian = true)
    {
        std::vector<uint8_t> bytes(s.begin(), s.end());

        return BytesToHexString(bytes, "");
    }
};

}
#endif //__KIT_NET_DATA_H__
