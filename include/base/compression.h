/**
 * @file compression.h
 * @brief 压缩模块
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-07 14:47:08
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_COMPRESSION_H__
#define __KIT_COMPRESSION_H__

#include "base/span.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace kit_muduo {


enum class CompressionResultStatus
{
    kOk,

    // 调用错误
    kInvalidArgument,
    kInvalidState,
    kUnsupportedOption,

    // 资源和初始化
    kInitializationFailed,
    kOutOfMemory,

    // 压缩执行错误
    kCompressionFailed,
    kDecompressionFailed,
    kIncompleteInput,
    kCorruptedInput,

    // 输出接收端错误
    kOutputFailed,

    // 无法归入以上类型的内部错误
    kInternalError,
};

struct CompressionResult
{
    CompressionResultStatus status{CompressionResultStatus::kOk};
    std::string message;

    /// @brief 输入到压缩器中的字节数
    uint64_t input_bytes{0};
    /// @brief 压缩器输出的字节数, 注意不是回调成功处理的字节数
    uint64_t output_bytes{0};

    bool ok() const noexcept { return status == CompressionResultStatus::kOk; }

    static CompressionResult Ok(
        uint64_t input_bytes = 0,
        uint64_t output_bytes = 0)
    {
        return CompressionResult{
            CompressionResultStatus::kOk,
            {},
            input_bytes,
            output_bytes
        };
    }

    static CompressionResult Failure(
        CompressionResultStatus status,
        std::string message,
        uint64_t input_bytes = 0,
        uint64_t output_bytes = 0)
    {
        return CompressionResult{
            status,
            std::move(message),
            input_bytes,
            output_bytes
        };
    }
};


struct CompressionCallbackResult 
{
    bool success{true};
    std::string message;

    bool ok() const noexcept { return success; }
    static CompressionCallbackResult Ok()
    {
        return {};
    }

    static CompressionCallbackResult Failure(std::string message)
    {
        return CompressionCallbackResult{
            false,
            std::move(message)
        };
    }
};


using CompressionCallback = std::function<CompressionCallbackResult(Span<const uint8_t>)>;


class StreamCompressor 
{
public:
    virtual ~StreamCompressor() = default;

    virtual CompressionResult write(Span<const uint8_t> input,
        const CompressionCallback& cb) = 0;

    virtual CompressionResult finish(const CompressionCallback& cb) = 0;

    virtual bool isFinished() const noexcept = 0;

    virtual bool failed() const noexcept = 0;
};

class StreamDecompressor
{
public:
    virtual ~StreamDecompressor() = default;

    virtual CompressionResult write(Span<const uint8_t> input, const CompressionCallback& cb) = 0;

    virtual CompressionResult finish(const CompressionCallback& cb) = 0;

    virtual bool isFinished() const noexcept = 0;

    virtual bool failed() const noexcept = 0;

};

class CompressionCodec
{
public:
    virtual ~CompressionCodec() = default;

    virtual CompressionResult compress(Span<const uint8_t> input,
        const CompressionCallback& cb) = 0;

    virtual CompressionResult decompress(Span<const uint8_t> input,
        const CompressionCallback& cb) = 0;

    virtual std::unique_ptr<StreamCompressor> createCompressor() = 0;

    virtual std::unique_ptr<StreamDecompressor> createDecompressor() = 0;

    virtual std::string suffix() const noexcept = 0;

    static std::unique_ptr<CompressionCodec> Create();

};


}
#endif // __KIT_COMPRESSION_H__