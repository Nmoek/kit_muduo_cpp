/**
 * @file test_compression.cpp
 * @brief 压缩模块测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-09 16:07:43
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/zstd_compression.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kit_muduo;

namespace {

Span<const uint8_t> AsSpan(const std::vector<uint8_t>& bytes)
{
    return {bytes.data(), bytes.size()};
}

CompressionCallback AppendTo(std::vector<uint8_t>& output,
    size_t* callback_count = nullptr)
{
    return [&output, callback_count](Span<const uint8_t> bytes) {
        output.insert(output.end(), bytes.begin(), bytes.end());
        if (callback_count != nullptr)
        {
            ++(*callback_count);
        }
        return CompressionCallbackResult::Ok();
    };
}

std::vector<uint8_t> MakePayload(size_t size)
{
    std::vector<uint8_t> payload(size);
    uint32_t state = 0x9e3779b9U;
    for (size_t index = 0; index < size; ++index)
    {
        state = state * 1664525U + 1013904223U;
        payload[index] = static_cast<uint8_t>(state >> 24U);
    }
    return payload;
}

CompressionResult CompressInto(CompressionCodec& codec,
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output)
{
    return codec.compress(AsSpan(input), AppendTo(output));
}

CompressionResult DecompressInto(CompressionCodec& codec,
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output)
{
    return codec.decompress(AsSpan(input), AppendTo(output));
}

} // namespace

/**
 * 测试思路：
 * 1. 使用包含 NUL 和非 ASCII 字节的确定性数据，验证接口按二进制处理。
 * 2. 一次性压缩后再一次性解压，结果必须逐字节一致。
 * 3. input_bytes 和 output_bytes 必须等于 codec 实际消费和交付的字节数。
 *
 * 示例：
 *   binary payload -> compress -> zstd frame -> decompress -> original payload
 */
TEST(TestZstdCompression, OneShotRoundTripPreservesBinaryPayload)
{
    auto payload = MakePayload(64 * 1024);
    payload[0] = 0;
    payload[1] = 0xff;

    ZstdCompressionCodec codec;
    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);

    ASSERT_TRUE(compress_result.ok()) << compress_result.message;
    EXPECT_EQ(compress_result.input_bytes, payload.size());
    EXPECT_EQ(compress_result.output_bytes, compressed.size());
    EXPECT_FALSE(compressed.empty());

    std::vector<uint8_t> decompressed;
    const auto decompress_result = DecompressInto(codec, compressed, decompressed);

    ASSERT_TRUE(decompress_result.ok()) << decompress_result.message;
    EXPECT_EQ(decompress_result.input_bytes, compressed.size());
    EXPECT_EQ(decompress_result.output_bytes, payload.size());
    EXPECT_EQ(decompressed, payload);
}

/**
 * 测试思路：
 * 1. 空原文也必须由 compressor 的 finish() 生成一个合法 zstd frame。
 * 2. 解压该 frame 时不产生原文字节，但必须成功抵达 frame 边界。
 *
 * 示例：
 *   empty payload -> non-empty zstd frame -> empty payload
 */
TEST(TestZstdCompression, EmptyPayloadProducesValidFrame)
{
    const std::vector<uint8_t> payload;
    ZstdCompressionCodec codec;

    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;
    EXPECT_EQ(compress_result.input_bytes, 0U);
    EXPECT_EQ(compress_result.output_bytes, compressed.size());
    EXPECT_FALSE(compressed.empty());

    std::vector<uint8_t> decompressed;
    const auto decompress_result = DecompressInto(codec, compressed, decompressed);
    ASSERT_TRUE(decompress_result.ok()) << decompress_result.message;
    EXPECT_EQ(decompress_result.input_bytes, compressed.size());
    EXPECT_EQ(decompress_result.output_bytes, 0U);
    EXPECT_TRUE(decompressed.empty());
}

/**
 * 测试思路：
 * 1. 原文大于 ZSTD_DStreamOutSize()，强制解压器多次交付输出块。
 * 2. 验证 callback 分块是传输细节，拼接后的业务输出仍完整一致。
 *
 * 示例：
 *   512 KiB payload -> [output chunk 1][chunk 2]... -> 512 KiB payload
 */
TEST(TestZstdCompression, LargePayloadIsDeliveredInMultipleOutputChunks)
{
    const auto payload = MakePayload(512 * 1024);
    ZstdCompressionCodec codec;

    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;

    size_t callback_count = 0;
    std::vector<uint8_t> decompressed;
    const auto decompress_result = codec.decompress(
        AsSpan(compressed), AppendTo(decompressed, &callback_count));

    ASSERT_TRUE(decompress_result.ok()) << decompress_result.message;
    EXPECT_GT(callback_count, 1U);
    EXPECT_EQ(decompress_result.output_bytes, payload.size());
    EXPECT_EQ(decompressed, payload);
}

/**
 * 测试思路：
 * 1. 将一份原文拆成多个 write()，模拟日志 batch 顺序推进同一压缩流。
 * 2. 累加每次调用的统计，并在 finish() 后解压最终 frame。
 * 3. write() 阶段允许暂时没有输出，不能据此判断压缩失败。
 *
 * 示例：
 *   [17 bytes][4093 bytes][remaining bytes] -> one zstd frame
 */
TEST(TestZstdCompression, StreamCompressorAcceptsMultipleWrites)
{
    const auto payload = MakePayload(256 * 1024 + 37);
    ZstdCompressionCodec codec;
    auto compressor = codec.createCompressor();
    ASSERT_NE(compressor, nullptr);

    std::vector<uint8_t> compressed;
    const auto callback = AppendTo(compressed);
    uint64_t consumed = 0;
    uint64_t produced = 0;

    size_t offset = 0;
    const size_t chunk_sizes[] = {17, 4093, 65536};
    for (const size_t requested_size : chunk_sizes)
    {
        const size_t size = std::min(requested_size, payload.size() - offset);
        const auto result = compressor->write(
            {payload.data() + offset, size}, callback);
        ASSERT_TRUE(result.ok()) << result.message;
        consumed += result.input_bytes;
        produced += result.output_bytes;
        offset += size;
    }

    const auto write_result = compressor->write(
        {payload.data() + offset, payload.size() - offset}, callback);
    ASSERT_TRUE(write_result.ok()) << write_result.message;
    consumed += write_result.input_bytes;
    produced += write_result.output_bytes;

    EXPECT_FALSE(compressor->isFinished());
    EXPECT_FALSE(compressor->failed());

    const auto finish_result = compressor->finish(callback);
    ASSERT_TRUE(finish_result.ok()) << finish_result.message;
    consumed += finish_result.input_bytes;
    produced += finish_result.output_bytes;

    EXPECT_TRUE(compressor->isFinished());
    EXPECT_FALSE(compressor->failed());
    EXPECT_EQ(consumed, payload.size());
    EXPECT_EQ(produced, compressed.size());

    std::vector<uint8_t> decompressed;
    const auto decompress_result = DecompressInto(codec, compressed, decompressed);
    ASSERT_TRUE(decompress_result.ok()) << decompress_result.message;
    EXPECT_EQ(decompressed, payload);
}

/**
 * 测试思路：
 * 1. 每次只向解压器提供一个压缩字节，覆盖 header、block 和 checksum 跨块。
 * 2. write() 成功只代表当前字节处理成功，最终完整性由 finish() 确认。
 * 3. 所有 write() 的 input_bytes 总和必须覆盖完整压缩输入。
 *
 * 示例：
 *   zstd frame -> [1 byte][1 byte]...[1 byte] -> original payload
 */
TEST(TestZstdCompression, StreamDecompressorAcceptsOneByteWrites)
{
    const auto payload = MakePayload(192 * 1024 + 11);
    ZstdCompressionCodec codec;

    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;

    auto decompressor = codec.createDecompressor();
    ASSERT_NE(decompressor, nullptr);

    std::vector<uint8_t> decompressed;
    const auto callback = AppendTo(decompressed);
    uint64_t consumed = 0;
    uint64_t produced = 0;
    for (size_t index = 0; index < compressed.size(); ++index)
    {
        const auto result = decompressor->write(
            {compressed.data() + index, 1}, callback);
        ASSERT_TRUE(result.ok()) << "offset=" << index << ": " << result.message;
        consumed += result.input_bytes;
        produced += result.output_bytes;
    }

    EXPECT_FALSE(decompressor->isFinished());
    EXPECT_FALSE(decompressor->failed());

    const auto finish_result = decompressor->finish(callback);
    ASSERT_TRUE(finish_result.ok()) << finish_result.message;
    produced += finish_result.output_bytes;

    EXPECT_TRUE(decompressor->isFinished());
    EXPECT_FALSE(decompressor->failed());
    EXPECT_EQ(consumed, compressed.size());
    EXPECT_EQ(produced, payload.size());
    EXPECT_EQ(decompressed, payload);
}

/**
 * 测试思路：
 * 1. 完整 frame 解码后追加一个空块，模拟文件或网络读取返回空数据。
 * 2. 空 write() 必须是 no-op，不能让解压器开始等待不存在的下一 frame。
 *
 * 示例：
 *   complete frame -> write(empty) -> finish succeeds
 */
TEST(TestZstdCompression, EmptyWritePreservesCompletedFrameState)
{
    const auto payload = MakePayload(4096);
    ZstdCompressionCodec codec;

    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;

    auto decompressor = codec.createDecompressor();
    std::vector<uint8_t> decompressed;
    const auto callback = AppendTo(decompressed);

    const auto write_result = decompressor->write(AsSpan(compressed), callback);
    ASSERT_TRUE(write_result.ok()) << write_result.message;

    const auto empty_result = decompressor->write({}, callback);
    ASSERT_TRUE(empty_result.ok()) << empty_result.message;
    EXPECT_EQ(empty_result.input_bytes, 0U);
    EXPECT_EQ(empty_result.output_bytes, 0U);

    const auto finish_result = decompressor->finish(callback);
    EXPECT_TRUE(finish_result.ok()) << finish_result.message;
    EXPECT_EQ(decompressed, payload);
}

/**
 * 测试思路：
 * 1. 分别压缩两份原文，产生两个相互独立的合法 zstd frame。
 * 2. 将两个 frame 直接拼接后一次解压，验证 rc==0 后仍处理剩余输入。
 *
 * 示例：
 *   frame(A) || frame(B) -> payload(A) || payload(B)
 */
TEST(TestZstdCompression, ConcatenatedFramesAreDecodedInOrder)
{
    const auto first_payload = MakePayload(8193);
    auto second_payload = MakePayload(16385);
    std::reverse(second_payload.begin(), second_payload.end());

    ZstdCompressionCodec codec;
    std::vector<uint8_t> first_frame;
    std::vector<uint8_t> second_frame;
    const auto first_result = CompressInto(codec, first_payload, first_frame);
    const auto second_result = CompressInto(codec, second_payload, second_frame);
    ASSERT_TRUE(first_result.ok()) << first_result.message;
    ASSERT_TRUE(second_result.ok()) << second_result.message;

    std::vector<uint8_t> concatenated = first_frame;
    concatenated.insert(concatenated.end(), second_frame.begin(), second_frame.end());

    std::vector<uint8_t> decompressed;
    const auto result = DecompressInto(codec, concatenated, decompressed);
    ASSERT_TRUE(result.ok()) << result.message;

    std::vector<uint8_t> expected = first_payload;
    expected.insert(expected.end(), second_payload.begin(), second_payload.end());
    EXPECT_EQ(result.input_bytes, concatenated.size());
    EXPECT_EQ(result.output_bytes, expected.size());
    EXPECT_EQ(decompressed, expected);
}

/**
 * 测试思路：
 * 1. 删除合法 frame 的最后一个字节，模拟文件截断或网络提前结束。
 * 2. write() 可以成功消费已有片段，但 finish() 必须发现未抵达 frame 边界。
 * 3. 不完整输入是终止失败，之后不能继续复用该解压器。
 *
 * 示例：
 *   complete frame - last byte -> write succeeds -> finish kIncompleteInput
 */
TEST(TestZstdCompression, TruncatedFrameIsRejectedAtFinish)
{
    const auto payload = MakePayload(32 * 1024);
    ZstdCompressionCodec codec;
    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;
    ASSERT_GT(compressed.size(), 1U);
    compressed.pop_back();

    auto decompressor = codec.createDecompressor();
    std::vector<uint8_t> decompressed;
    const auto callback = AppendTo(decompressed);

    const auto write_result = decompressor->write(AsSpan(compressed), callback);
    ASSERT_TRUE(write_result.ok()) << write_result.message;

    const auto finish_result = decompressor->finish(callback);
    EXPECT_EQ(finish_result.status, CompressionResultStatus::kIncompleteInput);
    EXPECT_TRUE(decompressor->isFinished());
    EXPECT_TRUE(decompressor->failed());

    const auto retry_result = decompressor->write({}, callback);
    EXPECT_EQ(retry_result.status, CompressionResultStatus::kInvalidState);
}

/**
 * 测试思路：
 * 1. 默认开启 frame checksum，篡改最后一个 checksum 字节。
 * 2. 解压器必须把 zstd checksum 错误映射为 kCorruptedInput。
 *
 * 示例：
 *   valid frame + damaged checksum -> kCorruptedInput
 */
TEST(TestZstdCompression, CorruptedChecksumIsRejected)
{
    const auto payload = MakePayload(32 * 1024);
    ZstdCompressionCodec codec;
    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;
    ASSERT_GE(compressed.size(), 4U);
    compressed.back() ^= 0xffU;

    std::vector<uint8_t> decompressed;
    const auto result = DecompressInto(codec, compressed, decompressed);

    EXPECT_EQ(result.status, CompressionResultStatus::kCorruptedInput);
}

/**
 * 测试思路：
 * 1. compressor 正常 finish 后进入不可逆终态。
 * 2. finish 后再次 write 或 finish 都必须返回 kInvalidState。
 *
 * 示例：
 *   write -> finish -> {write, finish} == kInvalidState
 */
TEST(TestZstdCompression, CompressorRejectsCallsAfterFinish)
{
    const auto payload = MakePayload(1024);
    ZstdCompressionCodec codec;
    auto compressor = codec.createCompressor();
    std::vector<uint8_t> compressed;
    const auto callback = AppendTo(compressed);

    const auto write_result = compressor->write(AsSpan(payload), callback);
    ASSERT_TRUE(write_result.ok()) << write_result.message;
    const auto finish_result = compressor->finish(callback);
    ASSERT_TRUE(finish_result.ok()) << finish_result.message;
    EXPECT_TRUE(compressor->isFinished());
    EXPECT_FALSE(compressor->failed());

    const auto second_write = compressor->write(AsSpan(payload), callback);
    const auto second_finish = compressor->finish(callback);
    EXPECT_EQ(second_write.status, CompressionResultStatus::kInvalidState);
    EXPECT_EQ(second_finish.status, CompressionResultStatus::kInvalidState);
}

/**
 * 测试思路：
 * 1. decompressor 正常 finish 后进入不可逆终态。
 * 2. finish 后再次 write 或 finish 都必须返回 kInvalidState。
 *
 * 示例：
 *   write(frame) -> finish -> {write, finish} == kInvalidState
 */
TEST(TestZstdCompression, DecompressorRejectsCallsAfterFinish)
{
    const auto payload = MakePayload(1024);
    ZstdCompressionCodec codec;
    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;

    auto decompressor = codec.createDecompressor();
    std::vector<uint8_t> decompressed;
    const auto callback = AppendTo(decompressed);
    const auto write_result = decompressor->write(AsSpan(compressed), callback);
    ASSERT_TRUE(write_result.ok()) << write_result.message;
    const auto finish_result = decompressor->finish(callback);
    ASSERT_TRUE(finish_result.ok()) << finish_result.message;
    EXPECT_TRUE(decompressor->isFinished());
    EXPECT_FALSE(decompressor->failed());

    const auto second_write = decompressor->write(AsSpan(compressed), callback);
    const auto second_finish = decompressor->finish(callback);
    EXPECT_EQ(second_write.status, CompressionResultStatus::kInvalidState);
    EXPECT_EQ(second_finish.status, CompressionResultStatus::kInvalidState);
}

/**
 * 测试思路：
 * 1. 输出接收端主动拒绝压缩结果，模拟归档文件 append 失败。
 * 2. codec 必须返回 kOutputFailed，且 stream 进入 failed 终态。
 * 3. output_bytes 记录 codec 已产生并尝试交付的字节数。
 *
 * 示例：
 *   compressor -> callback failure -> kOutputFailed -> stream unusable
 */
TEST(TestZstdCompression, CompressorPropagatesOutputFailure)
{
    ZstdCompressionCodec codec;
    auto compressor = codec.createCompressor();
    const CompressionCallback reject_output = [](Span<const uint8_t>) {
        return CompressionCallbackResult::Failure("archive append failed");
    };

    const auto finish_result = compressor->finish(reject_output);
    EXPECT_EQ(finish_result.status, CompressionResultStatus::kOutputFailed);
    EXPECT_EQ(finish_result.message, "archive append failed");
    EXPECT_GT(finish_result.output_bytes, 0U);
    EXPECT_TRUE(compressor->isFinished());
    EXPECT_TRUE(compressor->failed());

    const auto retry_result = compressor->write({}, reject_output);
    EXPECT_EQ(retry_result.status, CompressionResultStatus::kInvalidState);
}

/**
 * 测试思路：
 * 1. 解压输出接收端主动失败，模拟业务写入目标不可用。
 * 2. 解压器必须返回 kOutputFailed，而不是误报 zstd 数据损坏。
 * 3. 失败后的 context 不允许继续使用。
 *
 * 示例：
 *   decompressor -> callback failure -> kOutputFailed -> stream unusable
 */
TEST(TestZstdCompression, DecompressorPropagatesOutputFailure)
{
    const auto payload = MakePayload(4096);
    ZstdCompressionCodec codec;
    std::vector<uint8_t> compressed;
    const auto compress_result = CompressInto(codec, payload, compressed);
    ASSERT_TRUE(compress_result.ok()) << compress_result.message;

    auto decompressor = codec.createDecompressor();
    const CompressionCallback reject_output = [](Span<const uint8_t>) {
        return CompressionCallbackResult::Failure("consumer unavailable");
    };

    const auto result = decompressor->write(AsSpan(compressed), reject_output);
    EXPECT_EQ(result.status, CompressionResultStatus::kOutputFailed);
    EXPECT_EQ(result.message, "consumer unavailable");
    EXPECT_EQ(result.input_bytes, compressed.size());
    EXPECT_GT(result.output_bytes, 0U);
    EXPECT_FALSE(decompressor->isFinished());
    EXPECT_TRUE(decompressor->failed());

    const auto finish_result = decompressor->finish(reject_output);
    EXPECT_EQ(finish_result.status, CompressionResultStatus::kInvalidState);
}

/**
 * 测试思路：
 * 1. callback 抛异常时，异常不能穿透压缩模块边界。
 * 2. Execute() 应将异常转换为 kOutputFailed，并保留异常消息用于诊断。
 *
 * 示例：
 *   callback throws runtime_error("disk error") -> kOutputFailed
 */
TEST(TestZstdCompression, CallbackExceptionIsConvertedToOutputFailure)
{
    const auto payload = MakePayload(4096);
    ZstdCompressionCodec codec;
    const CompressionCallback throwing_output = [](Span<const uint8_t>)
        -> CompressionCallbackResult {
        throw std::runtime_error("disk error");
    };

    const auto result = codec.compress(AsSpan(payload), throwing_output);

    EXPECT_EQ(result.status, CompressionResultStatus::kOutputFailed);
    EXPECT_NE(result.message.find("disk error"), std::string::npos);
}

/**
 * 测试思路：
 * 1. zstd 压缩等级必须在底层库报告的合法范围内。
 * 2. 初始化阶段的配置错误按既定契约抛 invalid_argument。
 *
 * 示例：
 *   compression_level=INT32_MAX -> invalid_argument
 */
TEST(TestZstdCompression, InvalidCompressionLevelIsRejectedAtConstruction)
{
    ZstdCodecOptions options;
    options.compression_level = std::numeric_limits<int32_t>::max();

    EXPECT_THROW(ZstdCompressionCodec codec(options), std::invalid_argument);
}

/**
 * 测试思路：
 * 1. 没有任何 zstd frame 的空压缩输入不是“空原文 frame”。
 * 2. 空 write() 是 no-op，但 finish() 必须报告当前流从未完整结束。
 *
 * 示例：
 *   no frame bytes -> finish -> kIncompleteInput
 */
TEST(TestZstdCompression, MissingFrameIsReportedAsIncompleteInput)
{
    ZstdCompressionCodec codec;
    auto decompressor = codec.createDecompressor();
    std::vector<uint8_t> output;
    const auto callback = AppendTo(output);

    const auto write_result = decompressor->write({}, callback);
    ASSERT_TRUE(write_result.ok()) << write_result.message;

    const auto finish_result = decompressor->finish(callback);
    EXPECT_EQ(finish_result.status, CompressionResultStatus::kIncompleteInput);
    EXPECT_TRUE(decompressor->isFinished());
    EXPECT_TRUE(decompressor->failed());
}

/**
 * 测试思路：
 * 1. CompressionCodec::Create() 当前应装配一个可用的默认 codec。
 * 2. 两个独立创建的 codec/stream 必须各自维护状态，互不污染。
 *
 * 示例：
 *   codec A round trip + codec B round trip -> both preserve their payload
 */
TEST(TestZstdCompression, FactoryCreatesIndependentUsableCodecs)
{
    auto first_codec = CompressionCodec::Create();
    auto second_codec = CompressionCodec::Create();
    ASSERT_NE(first_codec, nullptr);
    ASSERT_NE(second_codec, nullptr);

    const auto first_payload = MakePayload(7777);
    auto second_payload = MakePayload(8888);
    std::reverse(second_payload.begin(), second_payload.end());

    std::vector<uint8_t> first_compressed;
    std::vector<uint8_t> second_compressed;
    const auto first_compress = CompressInto(
        *first_codec, first_payload, first_compressed);
    const auto second_compress = CompressInto(
        *second_codec, second_payload, second_compressed);
    ASSERT_TRUE(first_compress.ok()) << first_compress.message;
    ASSERT_TRUE(second_compress.ok()) << second_compress.message;

    std::vector<uint8_t> first_output;
    std::vector<uint8_t> second_output;
    const auto first_decompress = DecompressInto(
        *first_codec, first_compressed, first_output);
    const auto second_decompress = DecompressInto(
        *second_codec, second_compressed, second_output);
    ASSERT_TRUE(first_decompress.ok()) << first_decompress.message;
    ASSERT_TRUE(second_decompress.ok()) << second_decompress.message;

    EXPECT_EQ(first_output, first_payload);
    EXPECT_EQ(second_output, second_payload);
}
