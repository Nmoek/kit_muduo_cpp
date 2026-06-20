/**
 * @file bytes_codec.h
 * @brief 纯数据格式序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-18 10:26:08
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_BYTES_CODEC_H__
#define __KIT_BYTES_CODEC_H__

#include <exception>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace kit_muduo {

struct BytesView
{
    const uint8_t *data{nullptr};
    size_t size{0};

    bool empty() const { return size == 0; }
};

enum class CodecErrorCode
{
    kOk,
    kEmptyInput,
    kInvalidData,
    kUnsupportedTarget,
    kEncodeFailed,
    kDecodeFailed,
    kInternalError,
};

struct CodecResult
{
    bool ok{false};
    CodecErrorCode code{CodecErrorCode::kOk};
    std::string message;

    static CodecResult Success()
    {
        return {true, CodecErrorCode::kOk, "success"};
    }

    static CodecResult Failed(CodecErrorCode code, std::string message)
    {
        return {false, code, std::move(message)};
    }
};


class JsonCodec
{
public:
    static CodecResult Parse(BytesView view,  nlohmann::json &out);
    static CodecResult Validate(BytesView view);

    template<typename T>
    static CodecResult Decode(BytesView view, T &out)
    {
        nlohmann::json root;
        auto result = Parse(view, root);
        if(!result.ok)
        {
            return result;
        }

        try {

            root.get_to<T>(out);

        } catch(const std::exception &e) {

            return CodecResult::Failed(CodecErrorCode::kDecodeFailed, std::string("json decode failed: ") + e.what());

        }
        return CodecResult::Success();
    }

    template<typename T>
    static CodecResult Encode(const T& in, std::vector<uint8_t> &data)
    {
        try {
            const auto& root = nlohmann::json(in);
            const auto& s = root.dump();
            data.assign(s.begin(), s.end());
        } catch(const std::exception &e) {
            return CodecResult::Failed(CodecErrorCode::kEncodeFailed, std::string("json encode failed: ") + e.what());
        }
        return CodecResult::Success();
    }
};

class TextCodec
{
public:
    static CodecResult Decode(BytesView view, std::string &out);
    static CodecResult Encode(const std::string &in, std::vector<uint8_t> &data);
};


// 注意: 第一版仅实现校验逻辑, 序列化/反序列化暂时用不到
class XmlCodec
{
public:
    static CodecResult Validate(BytesView view);
};


}
#endif //__KIT_BYTES_CODEC_H__