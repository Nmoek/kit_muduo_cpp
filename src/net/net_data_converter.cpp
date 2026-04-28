/**
 * @file net_data_converter.cpp
 * @brief 二进网络数据处理转换器
 * @author ljk5
 * @version 1.0
 * @date 2025-11-07 15:32:56
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "net/net_log.h"

#include "net/net_data_converter.h"


namespace kit_muduo {


NetDataType NetDataType::FromString(const std::string &type_str)
{
#define XX(name) \
    if(#name == type_str) { return NetDataType(NetDataType::E::name); }

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
    return NetDataType(NetDataType::E::UNDEF);
}

const char* NetDataType::toStrs() const
{
#define XX(name) \
    if(NetDataType::E::name == m_type) return #name; /* code */
        
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

    return "UNDEF";
}

size_t NetDataType::getTypeSize()
{
#define XX(name) \
    if(#name == toString()) { return sizeof(NetDataType::name); }

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
#undef XX
    // 注意字符串是复合体  不是单一元数据 不能直接返回长度
    return 0;
}


uint8_t HexCharToValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw std::invalid_argument("Invalid hex character");
}

char ValueToHexChar(uint8_t v)
{
    if (v >= 0 && v <= 9) return '0' + v;
    else if (v >= 10 && v <= 15) return 'A' + (v - 10);
    return '?';
}

// 验证字符串长度是否匹配字节长度
bool CheckHexString(const std::string& hex_str, size_t byte_len)
{
    // 每个字节用2个十六进制字符表示
    return hex_str.size() == byte_len * 2;
}

// 十六进制字符串转字节数组
// "H1234"默认要去掉起始的"H"
std::vector<uint8_t> HexStringToBytes(const std::string& hex_str)
{
    if(hex_str[0] != KIT_HEX_PREFIX[0])
    {
        throw std::invalid_argument("start not H, Hex String is invalid!");
    }
    
    std::vector<uint8_t> bytes;
    bytes.reserve((hex_str.size() - strlen(KIT_HEX_PREFIX)) / 2);


    for (int i = strlen(KIT_HEX_PREFIX);i < hex_str.size(); i += 2) 
    {
        uint8_t byte = 0;
        byte |= HexCharToValue(hex_str[i]);
        byte <<= 4;
        byte |= HexCharToValue(hex_str[i + 1]);
        bytes.emplace_back(byte);
    }

    
    return bytes;
}

std::string BytesToHexString(const std::vector<uint8_t> bytes, const std::string& prefix)
{
    std::string hex_str = "H";

    for(auto b : bytes)
    {
        hex_str += ValueToHexChar((b >> 4) & 0x0F);
        hex_str += ValueToHexChar(b & 0x0F);

        hex_str += prefix;
    }

    return hex_str.substr(0, hex_str.size() - prefix.size());
}


template<>
std::vector<uint8_t> ValueToBytes(std::string val, bool is_endian)
{
    return std::vector<uint8_t>(val.begin(), val.end());
}

template<>
std::string BytesToValue(const std::vector<uint8_t>& bytes, bool is_endian)
{
    return std::string(bytes.begin(), bytes.end());
}


}