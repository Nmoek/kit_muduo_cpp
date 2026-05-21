/**
 * @file custom_tcp_field_codec.cpp 自定义TCP字段编解码
 * @brief 
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 01:37:06
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_field_codec.h"
#include "domain/custom_tcp_field_model.h"
#include "domain/custom_tcp_field_type_traits.h"


#include <stdexcept>
#include <variant>
#include <vector>


using namespace kit_muduo;

namespace kit_domain {

namespace {
inline bool IsStringType(FieldType type)
    {
    return type == FieldType::kString;
    }
}

std::vector<uint8_t> EncodeField(const FieldSpec &spec, const FieldScalar &scalar)
{
    if(IsStringType(spec.type))
    {
        const auto* str = std::get_if<std::string>(&scalar);
        if(!str)
        {
            throw std::invalid_argument("FieldScalar type does not match FieldSpec type");

        }

        if(str->size() != spec.byte_len)
        {
            throw std::invalid_argument("string field byte_len mismatch");
        }
        
        return std::vector<uint8_t>(str->begin(), str->end());
    }

    return VisitFieldType(spec.type, EncodeVisitor{spec, scalar}); 
}

FieldScalar DecodeField(const FieldSpec &spec, const std::vector<uint8_t> &bytes)
{
    if(IsStringType(spec.type))
    {
        if(bytes.size() != spec.byte_len)
        {
            throw std::invalid_argument("string field byte_len mismatch");
        }

        return std::string(bytes.begin(), bytes.end());
    }

    return VisitFieldType(spec.type, DecodeVisitor{spec, bytes});
}







    



}