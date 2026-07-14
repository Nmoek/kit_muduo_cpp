/**
 * @file endian.h
 * @brief  字节序转换
 * @author ljk5
 * @version 1.0
 * @date 2025-11-07 11:13:03
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_ENDIAN_H__
#define __KIT_ENDIAN_H__

#include <endian.h>
#include <netinet/in.h>
#include <stdexcept>
#include <stdint.h>

#define KIT_LITTLE_ENDIAN   (1)
#define KIT_BIG_ENDIAN      (2)


#if __BYTE_ORDER == __LITTLE_ENDIAN
    #define KIT_BYTE_ORDER  (KIT_LITTLE_ENDIAN)
    #define KIT_IS_LOCAL_BIG_ENDIAN()  (false)
#else
    #define KIT_BYTE_ORDER  (KIT_BIG_ENDIAN)
    #define KIT_IS_LOCAL_BIG_ENDIAN()  (true)

#endif



namespace kit_muduo {


template<class T>
T SwapEndian(T& value)
{
    union {
        T value;
        uint8_t bytes[sizeof(T)];
    }src, dst;

    src.value = value;

    for(int i = 0;i < sizeof(T); ++i)
    {
        dst.bytes[i] = src.bytes[sizeof(T) - 1 - i];
    }
    
    value = dst.value;
    
    return dst.value;
}



#if KIT_BYTE_ORDER == KIT_LITTLE_ENDIAN // 小端

template<class T>
T SwapToBigEndian(T& value)
{
    throw std::invalid_argument("SwapToBigEndian type invalid");
}

template<>
inline uint8_t SwapToBigEndian(uint8_t& value)
{
    return value;
}

template<>
inline int8_t SwapToBigEndian(int8_t& value)
{
    return value;
}


template<>
inline uint16_t SwapToBigEndian(uint16_t& value)
{
    value = __bswap_16(value);
    return value;
}

template<>
inline int16_t SwapToBigEndian(int16_t& value)
{
    value = __bswap_16(value);
    return value;
}

template<>
inline uint32_t SwapToBigEndian(uint32_t& value)
{
    value = __bswap_32(value);
    return value;
}

template<>
inline int32_t SwapToBigEndian(int32_t& value)
{
    value = __bswap_32(value);
    return value;
}


template<>
inline uint64_t SwapToBigEndian(uint64_t& value)
{
    value = __bswap_64(value);
    return value;
}

template<>
inline int64_t SwapToBigEndian(int64_t& value)
{
    value = __bswap_64(value);
    return value;
}

template<class T>
T SwapToLittleEndian(T& value)
{
    return value;
}


#else  // 大端


template<class T>
T SwapToBigEndian(T& value)
{
    return value;
}

template<class T>
T SwapToLittleEndian(T& value)
{
    throw std::invalid_argument("SwapToLittleEndian type invalid");
}


template<>
inline uint8_t SwapToLittleEndian(uint8_t& value)
{
    return value;
}

template<>
inline int8_t SwapToLittleEndian(int8_t& value)
{
    return value;
}


template<>
inline uint16_t SwapToLittleEndian(uint16_t& value)
{
    value = __bswap_16(value);
    return value;
}

template<>
inline int16_t SwapToLittleEndian(int16_t& value)
{
    value = __bswap_16(value);
    return value;
}

template<>
inline uint32_t SwapToLittleEndian(uint32_t& value)
{
    value = __bswap_32(value);
    return value;
}

template<>
inline int32_t SwapToLittleEndian(int32_t& value)
{
    value = __bswap_32(value);
    return value;
}


template<>
inline uint64_t SwapToLittleEndian(uint64_t& value)
{
    value = __bswap_64(value);
    return value;
}

template<>
inline int64_t SwapToLittleEndian(int64_t& value)
{
    value = __bswap_64(value);
    return value;
}


#endif
}


#endif //__KIT_ENDIAN_H__