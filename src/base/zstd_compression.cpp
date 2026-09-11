/**
 * @file zstd_compression.cpp
 * @brief ZSTD库实现的压缩模块
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-08 22:02:32
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "base/zstd_compression.h"
#include "base/compression.h"
#include "zstd.h"
#include "base/base_log.h"

#include <exception>
#include <stdexcept>


namespace kit_muduo {

namespace {

class ZstdStreamCompressor final: public StreamCompressor
{
public:
    explicit ZstdStreamCompressor(ZstdCodecOptions options);
    ~ZstdStreamCompressor() override = default;


    CompressionResult write(Span<const uint8_t> input,
        const CompressionCallback& cb) override;

    CompressionResult finish(const CompressionCallback& cb) override;

    bool isFinished() const noexcept override { return finished_; }

    bool failed() const noexcept override { return failed_; }

private:
    CompressionResult drain(ZSTD_EndDirective directive, Span<const uint8_t> input,  const CompressionCallback& callback);

private:
    /**
     * @brief zstd压缩流式句柄RAII
     */
    struct ZSTDCCtxContextDeleter
    {
        void operator()(ZSTD_CCtx* context) const noexcept
        {
            if(context)
            {
                ZSTD_freeCCtx(context);
            }
        }
    };

    /// @brief zstd压缩流式句柄
    std::unique_ptr<ZSTD_CCtx, ZSTDCCtxContextDeleter> context_{nullptr};
    ZstdCodecOptions options_;
    std::vector<uint8_t> buffer_;
    bool finished_{false};
    bool failed_{false};

};


class ZstdStreamDecompressor final: public StreamDecompressor
{
public:
    explicit ZstdStreamDecompressor(ZstdCodecOptions options);
    ~ZstdStreamDecompressor() override = default;

    CompressionResult write(Span<const uint8_t> input, const CompressionCallback& cb) override;

    CompressionResult finish(const CompressionCallback& cb) override;

    bool isFinished() const noexcept override { return finished_; }

    bool failed() const noexcept override { return failed_; }


private:
    CompressionResult drain(Span<const uint8_t> input, const CompressionCallback& callback);

private:
    struct ZSTDDCtxDeleter
    {
        void operator()(ZSTD_DCtx *context) noexcept
        {
            if(context)
            {
                ZSTD_freeDCtx(context);
            }
        }
    };

    /// @brief zstd解压缩流式句柄
    std::unique_ptr<ZSTD_DCtx, ZSTDDCtxDeleter> context_{nullptr};
    // TODO 解压缩参数后续补充
    // ZstdCodecOptions options_;
    /// @brief 已解压缩数据缓冲
    std::vector<uint8_t> buffer_;
    bool frame_finished_{false};
    bool finished_{false};
    bool failed_{false};
};

CompressionCallbackResult Execute(
    const CompressionCallback& callback,
    Span<const uint8_t> bytes) noexcept
{
    if (!callback)
    {
        return CompressionCallbackResult::Failure("compression callback is empty");
    }
    try {
        return callback(bytes);
    } catch(const std::exception &e) {
        return CompressionCallbackResult::Failure(
        std::string("compression callback exception: ") + e.what());
    } catch (...) {
        return CompressionCallbackResult::Failure("compression callback unknown exception");
    }
}

CompressionResultStatus MapZstdDecompressionError(size_t result) noexcept
{
    switch (ZSTD_getErrorCode(result))
    {
    case ZSTD_error_prefix_unknown:
    case ZSTD_error_corruption_detected:
    case ZSTD_error_checksum_wrong:
    case ZSTD_error_literals_headerWrong:
    case ZSTD_error_dictionary_corrupted:
    case ZSTD_error_dictionary_wrong:
        return CompressionResultStatus::kCorruptedInput;

    case ZSTD_error_srcSize_wrong:
    case ZSTD_error_noForwardProgress_inputEmpty:
        return CompressionResultStatus::kIncompleteInput;

    case ZSTD_error_version_unsupported:
    case ZSTD_error_frameParameter_unsupported:
    case ZSTD_error_frameParameter_windowTooLarge:
    case ZSTD_error_parameter_unsupported:
    case ZSTD_error_parameter_combination_unsupported:
    case ZSTD_error_parameter_outOfBound:
        return CompressionResultStatus::kUnsupportedOption;

    case ZSTD_error_stage_wrong:
    case ZSTD_error_init_missing:
        return CompressionResultStatus::kInvalidState;

    case ZSTD_error_memory_allocation:
        return CompressionResultStatus::kOutOfMemory;

    default:
        return CompressionResultStatus::kDecompressionFailed;
    }
}


bool ValidateZstdOptions(ZstdCodecOptions options)
{
    ZSTD_bounds bounds = ZSTD_cParam_getBounds(ZSTD_c_compressionLevel);
    if(ZSTD_isError(bounds.error)) 
    {
        COMPRESS_F_ERROR("zstd compression param get bounds error: %s\n", ZSTD_getErrorName(bounds.error));
        return false;
    }

    if (options.compression_level < bounds.lowerBound
        || options.compression_level > bounds.upperBound)
    {
        COMPRESS_F_ERROR("zstd 'compressionLevel' is out of range\n");
        return false;
    }

    bounds = ZSTD_cParam_getBounds(ZSTD_c_checksumFlag);
    if(ZSTD_isError(bounds.error)) 
    {
        COMPRESS_F_ERROR("zstd compression param get bounds error: %s\n", ZSTD_getErrorName(bounds.error));
        return false;
    }

    if (options.compression_checksum < bounds.lowerBound
        || options.compression_checksum > bounds.upperBound)
    {
        COMPRESS_F_ERROR("zstd 'checksumFlag' is out of range\n");
        return false;
    }

    return true;
}

/**
 * @brief 压缩参数
 * @param ctx 
 */
inline void ZstdCCtxSetParamter(ZSTD_CCtx* ctx, ZSTD_cParameter param, int value)
{
    const size_t res = ZSTD_CCtx_setParameter(ctx, param, value);
    if(ZSTD_isError(res))
    {
        throw std::runtime_error("zstd compression context set params error: " + std::string(ZSTD_getErrorName(res)));
    }
}

/**
 * @brief 解压缩参数
 * @param ctx 
 */
inline void ZstdDCtxSetParamter(ZSTD_DCtx* ctx, ZSTD_dParameter param, int value)
{
    const size_t res = ZSTD_DCtx_setParameter(ctx, param, value);
    if(ZSTD_isError(res))
    {
        throw std::runtime_error("zstd decompression context set params error: " + std::string(ZSTD_getErrorName(res)));
    }
}

} // namespace


/*****************ZstdStreamCompressor******************/
ZstdStreamCompressor:: ZstdStreamCompressor(ZstdCodecOptions options)
    :context_(ZSTD_createCCtx())
    ,options_(std::move(options))
    ,buffer_(ZSTD_CStreamOutSize())
    ,failed_(false)
{
    if(!context_)
    {
        throw std::runtime_error("create zstd compression context error!");
    }

    if(!ValidateZstdOptions(options_))
    {
        throw std::invalid_argument("zstd compression paramter invalid");
    }

    ZstdCCtxSetParamter(context_.get(), ZSTD_c_compressionLevel, options_.compression_level);
    
    ZstdCCtxSetParamter(context_.get(), ZSTD_c_checksumFlag, static_cast<int>(options_.compression_checksum));
}


CompressionResult ZstdStreamCompressor::write(Span<const uint8_t> input,
    const CompressionCallback& cb)
{
    if (finished_ || failed_ || !context_)
    {
        return CompressionResult::Failure(CompressionResultStatus::kInvalidState, "zstd compressor is not writable");
    }
    auto result = drain(ZSTD_e_continue, input, cb);
    if(!result.ok())
    {
        failed_ = true;
    }
    return result;
}

CompressionResult ZstdStreamCompressor::finish(const CompressionCallback& cb)
{
    if(finished_ || failed_ || !context_)
    {
        return CompressionResult::Failure(CompressionResultStatus::kInvalidState,
            "zstd compressor is already finished");
    }
    finished_ = true;
    auto result = drain(ZSTD_e_end, {}, cb);
    if(!result.ok())
    {
        failed_ = true;
    }
    return result;
}

CompressionResult ZstdStreamCompressor::drain(ZSTD_EndDirective directive, Span<const uint8_t> input, const CompressionCallback& callback) 
{
    ZSTD_inBuffer in{input.data(), input.size(), 0};
    size_t remaining = directive == ZSTD_e_end ? 1 : 0;
    uint64_t produced = 0;

    while (in.pos < in.size || (directive == ZSTD_e_end && remaining != 0)) 
    {
        ZSTD_outBuffer out{buffer_.data(), buffer_.size(), 0};

        remaining = ZSTD_compressStream2(context_.get(), &out, &in, directive);
        if (ZSTD_isError(remaining))
        {
            return CompressionResult::Failure(CompressionResultStatus::kCompressionFailed,
                ZSTD_getErrorName(remaining), in.pos, produced);
        }
        if (out.pos != 0) 
        {

            produced += out.pos;
            const auto callback_result = Execute(callback, Span<const uint8_t>(buffer_.data(), out.pos));
            if (!callback_result.ok())
            {
                return CompressionResult::Failure(CompressionResultStatus::kOutputFailed,
                    callback_result.message, in.pos, produced);
            }
        }
    }
    return CompressionResult::Ok(in.pos, produced);
}


/*****************ZstdStreamDecompressor******************/
ZstdStreamDecompressor::ZstdStreamDecompressor(ZstdCodecOptions options)
    :context_(ZSTD_createDCtx())
    ,buffer_(ZSTD_DStreamOutSize())
    ,frame_finished_(false)
    ,finished_(false)
    ,failed_(false)
{
    if(!context_)
    {
        throw std::runtime_error("create zstd decompression context error!");
    }
    // TODO 解压缩参数暂时不设置
}


CompressionResult ZstdStreamDecompressor::write(Span<const uint8_t> input, const CompressionCallback& cb)
{
    if (finished_ || failed_ || !context_)
    {
        return CompressionResult::Failure(CompressionResultStatus::kInvalidState,
            "zstd decompressor is not writable");
    }
    
    if (input.size() == 0)
    {
        return CompressionResult::Ok();
    }
 
    auto result = drain(input, cb);
    if(!result.ok())
    {
        failed_ = true;
    }
    return result;
}

CompressionResult ZstdStreamDecompressor::finish(const CompressionCallback& cb)
{
    if (finished_ || failed_ || !context_)
    {
        return CompressionResult::Failure(CompressionResultStatus::kInvalidState,
            "zstd decompressor is not writable");
    }

    finished_ = true;
    if(!frame_finished_)
    {
        failed_ = true;
        return CompressionResult::Failure(CompressionResultStatus::kIncompleteInput, "zstd decompressesion stream is incomplete");
    }

    return CompressionResult::Ok();
}


CompressionResult ZstdStreamDecompressor::drain(Span<const uint8_t> input, const CompressionCallback& callback)
{
    ZSTD_inBuffer in{input.data(), input.size(), 0};
    uint64_t produced = 0;

    for(;;)
    {
        ZSTD_outBuffer out{buffer_.data(), buffer_.size(), 0};

        /* 特别注意：
            rc>0 表示当前压缩frame还没有完整结束，下一次提供的压缩输入大小建议值。
            rc==0 唯一标识当前压缩frame已经解压缩完成
        */
        const size_t rc = ZSTD_decompressStream(context_.get(), &out, &in);
        if (ZSTD_isError(rc))
        {
            return CompressionResult::Failure(MapZstdDecompressionError(rc),
                ZSTD_getErrorName(rc), in.pos, produced);
        }

        if (out.pos != 0)
        {
            produced += out.pos;

            const auto callback_result = Execute(callback, Span<const uint8_t>(buffer_.data(), out.pos));
            if (!callback_result.ok())
            {
                return CompressionResult::Failure(CompressionResultStatus::kOutputFailed,
                    callback_result.message, in.pos, produced);
            }
        }
        // 难点  怎么判断一段数据的解压缩已经完成了
        frame_finished_ = (0 == rc);
        // 情况1：当前frame解压缩完成
        if(0 == rc)
        {
            if(in.pos < in.size)
            {
                // 本次数据中有多个frame, 当前frame完成需要继续解析同段数据的下一个frame
                continue;
            }
            break;
        }
        // 情况2: 当前frame未解压缩完rc>0 且输入数据段还有剩余
        if(in.pos < in.size)
        {
            continue;
        }

        //情况3: 当前frame未解压缩完rc>0 且输入数据段全部消费，但用户输出缓冲满, 无法确定内部输出缓冲是否排空，继续尝试排空
        if(out.pos == out.size)
        {
            continue;
        }

        // 当前frame未解压缩完rc > 0 且输入数据段全部消费 且用户输出缓冲未写满
        // 内部当前无可输出数据，等待下一批压缩输入。
        break;
    } 

    // 返回成功只代表当前输入的这一段数据没有出错 不代表解压缩完成
    return CompressionResult::Ok(in.pos, produced);
}



/****************ZstdCompressionCodec*******************/

ZstdCompressionCodec::ZstdCompressionCodec(ZstdCodecOptions options)
    :options_(std::move(options))
{

    if(!ValidateZstdOptions(options_))
    {
        throw std::invalid_argument("zstd compression paramter invalid");
    }

}

CompressionResult ZstdCompressionCodec::compress(Span<const uint8_t> input,
    const CompressionCallback& cb)
{
    ZstdStreamCompressor stream(options_);

    const auto result = stream.write(input, cb);
    if (!result.ok())
    {
        return result;
    }
    auto finish_result = stream.finish(cb);
    finish_result.input_bytes += result.input_bytes;
    finish_result.output_bytes += result.output_bytes;
    return finish_result;
}

CompressionResult ZstdCompressionCodec::decompress(Span<const uint8_t> input,
    const CompressionCallback& cb)
{
    ZstdStreamDecompressor stream(options_);

    const auto result = stream.write(input, cb);
    if (!result.ok())
    {
        return result;
    }
    auto finished_result = stream.finish(cb);
    finished_result.input_bytes += result.input_bytes;
    finished_result.output_bytes += result.output_bytes;
    return finished_result;
}

std::unique_ptr<StreamCompressor> ZstdCompressionCodec::createCompressor()
{
    return std::make_unique<ZstdStreamCompressor>(options_);
}

std::unique_ptr<StreamDecompressor> ZstdCompressionCodec::createDecompressor()
{
    return std::make_unique<ZstdStreamDecompressor>(options_);
}









} // namespace kit_muduo 
