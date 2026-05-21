/**
 * @file custom_tcp_field_codec.h
 * @brief 自定义TCP字段编解码
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 01:32:47
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CUSTOM_TCP_FIELD_CODEC__
#define __KIT_CUSTOM_TCP_FIELD_CODEC__

#include "domain/custom_tcp_field_model.h"
#include "domain/custom_tcp_field_type_traits.h"
#include "net/endian.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kit_domain {


namespace {


template<class T>
void CheckFixedSize(const FieldSpec &spec, size_t actual_size)
{
    if(spec.byte_len != actual_size)
    {
        throw std::invalid_argument("field byte_len does not match FieldType fixed size");
    }
}

template<class T>
void CheckArithmeticByteLen(size_t byte_len)
{
    static_assert(std::is_arithmetic<T>::value, "T must be arithmetic type");

    if(byte_len != sizeof(T))
    {
        throw std::invalid_argument("numeric field byte_len mismatch");

    }

}


}


/**
 * @brief 将真真值转换为字节数组(默认按大端排列字节)
 * @tparam T 编译期类型
 * @param value 真值
 * @param byte_len 真值对应字节长度
 * @param will_order 真值期望的大小端字节序
 * @return std::vector<uint8_t> 
 */
template<class T>
std::vector<uint8_t> EncodeArithmetic(T value, size_t byte_len, FieldByteOrder will_order)
{
    static_assert(std::is_arithmetic<T>::value, "T must be arithmetic type");

    CheckArithmeticByteLen<T>(byte_len);

    if(FieldByteOrder::kRaw == will_order)
    {
        throw std::invalid_argument("numeric field cannot use raw byte order");
    }

    const auto* src = reinterpret_cast<const uint8_t*>(&value);
    std::vector<uint8_t> bytes(src, src + sizeof(T));

    // 注意: 本机小端情况下转一下先统一变为大端排列
#if KIT_BYTE_ORDER == KIT_LITTLE_ENDIAN
    std::reverse(bytes.begin(), bytes.end());
#endif

    // 注意: 这里的语义是期望输出小端排列
    if(FieldByteOrder::kLittleEndian == will_order)
    {
        std::reverse(bytes.begin(), bytes.end());
    }

    return bytes;
}

/**
 * @brief 将字节数组转换为真值
 * @tparam T 真值类型
 * @param bytes 字节数组
 * @param actual_order 字节数组按照什么字节序排列
 * @return T 
 */
template<class T>
T DecodeArithmetic(const std::vector<uint8_t> &bytes, FieldByteOrder actual_order)
{
    static_assert(std::is_arithmetic<T>::value, "T must be arithmetic");

    CheckArithmeticByteLen<T>(bytes.size());

    std::vector<uint8_t> tmp(bytes);

    // 注意：传入的字节数组实际是按小端排序需要先统一转为大端
    if(FieldByteOrder::kLittleEndian == actual_order)
    {
        std::reverse(tmp.begin(), tmp.end());
    }
    else if(FieldByteOrder::kRaw == actual_order)
    {
        throw std::invalid_argument("numeric field cannot use raw byte order");
    }

    T value{};
    auto *dst = reinterpret_cast<uint8_t*>(&value);


#if KIT_BYTE_ORDER == KIT_BIG_ENDIAN
    std::copy(tmp.begin(), tmp.end(), dst);
#else
    std::reverse_copy(tmp.begin(), tmp.end(), dst);
#endif
    return value;
}


struct EncodeVisitor
{
    const FieldSpec &spec;
    const FieldScalar &scalar;

    template<FieldType Type>
    std::vector<uint8_t> operator()() const 
    {
        using Traits = FieldTypeTraits<Type>;
        using CompileType = typename Traits::compile_type;

        static_assert(Traits::numeric, "non numeric FieldType should be handled before VisitFieldType");

        if(spec.byte_len != Traits::fixed_size)
        {
            throw std::invalid_argument("field byte_len does not match FieldType fixed size");
        }

        const auto* value = std::get_if<CompileType>(&scalar);
        if(!value)
        {
            throw std::invalid_argument("FieldScalar type does not match FieldSpec type");
        }

        return EncodeArithmetic<CompileType>(*value, spec.byte_len, spec.byte_order);
    }
};


std::vector<uint8_t> EncodeField(const FieldSpec& spec, const FieldScalar& scalar);


struct DecodeVisitor
{
    const FieldSpec &spec;
   const std::vector<uint8_t> bytes;

    template<FieldType Type>
    FieldScalar operator()() const 
    {
        using Traits = FieldTypeTraits<Type>;
        using CompileType = typename Traits::compile_type;

        static_assert(Traits::numeric, "non numeric FieldType should be handled before VisitFieldType");

        if(spec.byte_len != Traits::fixed_size)
        {
            throw std::invalid_argument("field byte_len does not match FieldType fixed size");
        }
        
        if(bytes.size() != spec.byte_len)
        {
            throw std::invalid_argument("field bytes size mismatch");
        }

        return DecodeArithmetic<CompileType>(bytes, spec.byte_order);
    }

};



FieldScalar DecodeField(const FieldSpec& spec, const std::vector<uint8_t>& bytes);


} // namespace kit_domain

#endif // __KIT_CUSTOM_TCP_FIELD_CODEC__
