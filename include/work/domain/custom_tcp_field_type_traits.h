/**
 * @file custom_tcp_field_type_traits.h
 * @brief  自定义TCP字段值类型萃取
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 01:08:18
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CUSTOM_TCP_FIELD_TYPE_TRAITS__
#define __KIT_CUSTOM_TCP_FIELD_TYPE_TRAITS__

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include "nlohmann/json.hpp"



namespace kit_domain {


/**
 * @brief 字段值类型对应编译期类型
 */
using FieldScalar = std::variant<
    int8_t, uint8_t,
    int16_t, uint16_t,
    int32_t, uint32_t,
    int64_t, uint64_t,
    float, double,
    std::string
>;

/**
 * @brief 字段值类型枚举
 */
enum class FieldType {
    kUndefine,
    kInt8, kUint8,
    kInt16, kUint16,
    kInt32, kUint32,
    kInt64, kUint64,
    kFloat, kDouble,
    kString,
};
// nlohann::json 序列化反序列化
NLOHMANN_JSON_SERIALIZE_ENUM(FieldType, {
    {FieldType::kInt8, "INT8"},
    {FieldType::kUint8, "UINT8"},
    {FieldType::kInt16, "INT16"},
    {FieldType::kUint16, "UINT16"},
    {FieldType::kInt32, "INT32"},
    {FieldType::kUint32, "UINT32"},
    {FieldType::kInt64, "INT64"},
    {FieldType::kUint64, "UINT64"},
    {FieldType::kFloat, "FLOAT"},
    {FieldType::kDouble, "DOUBLE"},
    {FieldType::kString, "STR"},
})

FieldType FieldTypeFromString(const std::string& str);
std::string FieldTypeToString(FieldType type);


/**
 * @brief 关键: 使用枚举值作为模版重载
 * @tparam Type 
 */
template<FieldType Type>
struct FieldTypeTraits;


template<>
struct FieldTypeTraits<FieldType::kInt8> 
{
    using compile_type = int8_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};


template<>
struct FieldTypeTraits<FieldType::kUint8> 
{
    using compile_type = uint8_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kInt16> 
{
    using compile_type = int16_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kUint16> 
{
    using compile_type = uint16_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kInt32> 
{
    using compile_type = int32_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kUint32> 
{
    using compile_type = uint32_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kInt64> 
{
    using compile_type = int64_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kUint64> 
{
    using compile_type = uint64_t;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kFloat> 
{
    using compile_type = float;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};

template<>
struct FieldTypeTraits<FieldType::kDouble> 
{
    using compile_type = double;
    static constexpr size_t fixed_size = sizeof(compile_type);
    static constexpr bool numeric = true;
};


template<>
struct FieldTypeTraits<FieldType::kString> 
{
    using compile_type = std::string;
    static constexpr size_t fixed_size = 0;
    static constexpr bool numeric = false;
};

/**
 * @brief 将字段值类型转换： 枚举值-->编译期值
 * @tparam Visitor 
 * @param type 
 * @param visitor 
 * @return decltype(auto) 
 */
template<typename Visitor>
decltype(auto) VisitFieldType(FieldType type, Visitor &&visitor)
{
    switch (type) 
    {
        case FieldType::kInt8: return visitor.template operator()<FieldType::kInt8>();
        case FieldType::kUint8: return visitor.template operator()<FieldType::kUint8>();
        case FieldType::kInt16: return visitor.template operator()<FieldType::kInt16>();
        case FieldType::kUint16: return visitor.template operator()<FieldType::kUint16>();
        case FieldType::kInt32: return visitor.template operator()<FieldType::kInt32>();
        case FieldType::kUint32: return visitor.template operator()<FieldType::kUint32>();
        case FieldType::kInt64: return visitor.template operator()<FieldType::kInt64>();
        case FieldType::kUint64: return visitor.template operator()<FieldType::kUint64>();
        case FieldType::kFloat: return visitor.template operator()<FieldType::kFloat>();
        case FieldType::kDouble: return visitor.template operator()<FieldType::kDouble>();
        // case FieldType::kString: return visitor.template operator()<FieldType::kString>();

        default:
            throw std::invalid_argument("unsupported FieldType");
    }
}

/**
 * @brief 定义一个重载组合器，用于将多个可调用对象（如 lambda）组合成一个对象。专门用于std::visit 优化，避免大量if-else判断编写
 * @tparam Ts 
 */
template<class... Ts>
struct ScalarToStringVisitorOverload : Ts... { using Ts::operator()...; };

template<class... Ts>
ScalarToStringVisitorOverload(Ts...) -> ScalarToStringVisitorOverload<Ts...>;

/**
 * @brief 真值转换为字符串
 * @param scalar 
 * @return std::string 
 */
std::string FieldScalarValToString(const FieldScalar &scalar);



}
#endif //__KIT_CUSTOM_TCP_FIELD_TYPE_TRAITS__