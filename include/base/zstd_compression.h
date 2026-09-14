/**
 * @file zstd_compression.h
 * @brief ZSTD库实现的压缩模块
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-08 21:53:52
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_ZSTD_COMPRESSION_H__
#define __KIT_ZSTD_COMPRESSION_H__

#include "base/compression.h"

namespace kit_muduo {

/**
 * @brief zstd配置
 */
struct ZstdCodecOptions
{
    /******压缩参数*****/
    /// @brief 压缩等级默认为3  1-3 快速   15-22高压缩比
    int32_t compression_level{1};
    /// @brief 开启帧数据校验和
    bool compression_checksum{true};

    /******TODO解压缩参数*****/
};

class ZstdCompressionCodec final: public CompressionCodec
{
public:
    explicit ZstdCompressionCodec(ZstdCodecOptions options = {});
    ~ZstdCompressionCodec() override = default;

    CompressionResult compress(Span<const uint8_t> input,
        const CompressionCallback& cb) override;

    CompressionResult decompress(Span<const uint8_t> input,
        const CompressionCallback& cb) override;

    std::unique_ptr<StreamCompressor> createCompressor() override;

    std::unique_ptr<StreamDecompressor> createDecompressor() override;

    std::string suffix() const noexcept override { return "zst"; }
private:
    ZstdCodecOptions options_;
};



}
#endif // __KIT_ZSTD_COMPRESSION_H__