/**
 * @file compression.cpp
 * @brief 
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-09 18:49:00
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/compression.h"
#include "base/zstd_compression.h"

namespace kit_muduo {



std::unique_ptr<CompressionCodec> CompressionCodec::Create()
{
    // TODO 目前只支持 zstd
    return std::make_unique<ZstdCompressionCodec>();
}











}