/**
 * @file bytes_codec.cpp
 * @brief 纯数据格式序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-18 10:50:59
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/base_log.h"
#include "base/bytes_codec.h"
#include "pugixml/pugixml.hpp"

#include <exception>
namespace kit_muduo {


CodecResult JsonCodec::Parse(BytesView view,  nlohmann::json &out)
{
    if(!view.data || view.empty())
    {
        return CodecResult::Failed(CodecErrorCode::kEmptyInput, "input data null");
    }

    try {
        out = nlohmann::json::parse(view.data, view.data + view.size);
    } catch(const std::exception &e) {
        return CodecResult::Failed(CodecErrorCode::kDecodeFailed, std::string("json parse error: ") + e.what());
    }
    return CodecResult::Success();
}

CodecResult JsonCodec::Validate(BytesView view)
{
    if(!view.data || view.empty())
    {
        return CodecResult::Failed(CodecErrorCode::kEmptyInput, "input data null");
    }

    try {
        if(!nlohmann::json::accept(view.data, view.data + view.size))
        {
            return CodecResult::Failed(CodecErrorCode::kInvalidData, "json data invalid");
        }
    } catch(const std::exception &e) {
        return CodecResult::Failed(CodecErrorCode::kDecodeFailed, std::string("json parse error: ") + e.what());
    }
    return CodecResult::Success();
}


CodecResult TextCodec::Decode(BytesView view, std::string &out)
{
    out.clear();
    if(nullptr != view.data && view.size > 0)
    {
        out.assign(view.data, view.data + view.size);
    }
    return CodecResult::Success();
}

CodecResult TextCodec::Encode(const std::string &in, std::vector<uint8_t> &data)
{
   data.assign(in.begin(), in.end());

    return CodecResult::Success();
}

CodecResult XmlCodec::Validate(BytesView view)
{
    if(!view.data || view.empty())
    {
        return CodecResult::Failed(CodecErrorCode::kEmptyInput, "input data null");
    }

    try {
        pugi::xml_document doc;
        auto xml_result = doc.load_buffer(view.data, view.size);
        if(pugi::xml_parse_status::status_ok != xml_result.status)
        {
            return CodecResult::Failed(CodecErrorCode::kInvalidData, "xml data invalid");
        }

    } catch(const std::exception &e) {
        return CodecResult::Failed(CodecErrorCode::kDecodeFailed, std::string("xml parse error: ") + e.what());
    }
    return CodecResult::Success();
}











}