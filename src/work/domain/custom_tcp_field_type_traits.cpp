/**
 * @file custom_tcp_field_type_traits.cpp
 * @brief 自定义TCP字段类型萃取
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 10:57:14
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_field_type_traits.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace kit_domain {

static const std::unordered_map<std::string, FieldType> g_str2type_map = {
    {"INT8", FieldType::kInt8}, 
    {"UINT8", FieldType::kUint8},
    {"INT16", FieldType::kInt16}, 
    {"UINT16", FieldType::kUint16},
    {"INT32", FieldType::kInt32}, 
    {"UINT32", FieldType::kUint32},
    {"INT64", FieldType::kInt64},
    {"UINT64", FieldType::kUint64},
    {"FLOAT", FieldType::kFloat},
    {"DOUBLE", FieldType::kDouble},
    {"STR", FieldType::kString},
};

static const std::unordered_map<FieldType, std::string> g_type2str_map = {
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
};


FieldType FieldTypeFromString(const std::string& str)
{
    return g_str2type_map.at(str);
}


std::string FieldTypeToString(FieldType type)
{
    return g_type2str_map.at(type);
}

std::string FieldScalarValToString(const FieldScalar &scalar)
{
    return std::visit(ScalarToStringVisitorOverload{
        [](uint8_t value){ return std::to_string(static_cast<int>(value)); },
        [](int8_t value) { return std::to_string(static_cast<int>(value)); },
        [](const std::string& value) { return value; },
        [](const auto& value) { return std::to_string(value); }
    }, scalar);
}


}