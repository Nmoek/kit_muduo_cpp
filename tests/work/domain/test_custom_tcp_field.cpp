/**
 * @file test_custom_tcp_field.cpp
 * @brief 自定义TCP字段测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 14:41:34
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../../test_log.h"
#include "domain/custom_tcp_field_codec.h"
#include "domain/custom_tcp_field_model.h"
#include "domain/custom_tcp_field_type_traits.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace kit_muduo;
using namespace kit_domain;

namespace {

FieldSpec MakeSpec(FieldType type, size_t byte_len,
                   FieldByteOrder byte_order = FieldByteOrder::kBigEndian)
{
    FieldSpec spec;
    spec.type = type;
    spec.byte_len = byte_len;
    spec.byte_order = byte_order;
    return spec;
}

template<class T>
void ExpectRoundTrip(const FieldSpec& spec,
                     const T& value,
                     const std::vector<uint8_t>& expected_bytes)
{
    const auto encode_bytes = EncodeField(spec, FieldScalar{value});
    EXPECT_EQ(encode_bytes, expected_bytes);

    const auto scalar = DecodeField(spec, encode_bytes);
    const auto* decoded_value = std::get_if<T>(&scalar);
    ASSERT_NE(decoded_value, nullptr);

    if constexpr (std::is_floating_point<T>::value) {
        EXPECT_EQ(*decoded_value, value);
    } else {
        EXPECT_EQ(*decoded_value, value);
    }
}

} // namespace

TEST(TestCustomTcpField, EncodeAndDecode)
{
    // 测试思路：1 字节整数没有端序差异。例：int8_t(123) 应编码为 0x7B，
    // uint8_t(234) 应编码为 0xEA，解码后 variant 中的实际类型和值都要一致。
    ExpectRoundTrip<int8_t>(
        MakeSpec(FieldType::kInt8, sizeof(int8_t)),
        static_cast<int8_t>(123),
        std::vector<uint8_t>{0x7B});
    ExpectRoundTrip<uint8_t>(
        MakeSpec(FieldType::kUint8, sizeof(uint8_t)),
        static_cast<uint8_t>(234),
        std::vector<uint8_t>{0xEA});

    // 测试思路：多字节整数默认按大端 wire bytes 编码。例：0x1234 写到网络流
    // 应为 {0x12, 0x34}；负数使用补码字节序，int16_t(-2) 应为 {0xFF, 0xFE}。
    ExpectRoundTrip<int16_t>(
        MakeSpec(FieldType::kInt16, sizeof(int16_t)),
        static_cast<int16_t>(-2),
        std::vector<uint8_t>{0xFF, 0xFE});
    ExpectRoundTrip<uint16_t>(
        MakeSpec(FieldType::kUint16, sizeof(uint16_t)),
        static_cast<uint16_t>(0x1234),
        std::vector<uint8_t>{0x12, 0x34});
    ExpectRoundTrip<int32_t>(
        MakeSpec(FieldType::kInt32, sizeof(int32_t)),
        static_cast<int32_t>(511981),
        std::vector<uint8_t>{0x00, 0x07, 0xCF, 0xED});
    ExpectRoundTrip<uint32_t>(
        MakeSpec(FieldType::kUint32, sizeof(uint32_t)),
        static_cast<uint32_t>(0x89ABCDEF),
        std::vector<uint8_t>{0x89, 0xAB, 0xCD, 0xEF});
    ExpectRoundTrip<int64_t>(
        MakeSpec(FieldType::kInt64, sizeof(int64_t)),
        static_cast<int64_t>(159818),
        std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x70, 0x4A});
    ExpectRoundTrip<uint64_t>(
        MakeSpec(FieldType::kUint64, sizeof(uint64_t)),
        static_cast<uint64_t>(0x0102030405060708ULL),
        std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});

    // 测试思路：little endian 字段只改变 wire bytes 排列，不改变业务真值。
    // 例：0x1234 小端写入应为 {0x34, 0x12}，解码仍应得到 0x1234。
    ExpectRoundTrip<uint16_t>(
        MakeSpec(FieldType::kUint16, sizeof(uint16_t), FieldByteOrder::kLittleEndian),
        static_cast<uint16_t>(0x1234),
        std::vector<uint8_t>{0x34, 0x12});
    ExpectRoundTrip<uint32_t>(
        MakeSpec(FieldType::kUint32, sizeof(uint32_t), FieldByteOrder::kLittleEndian),
        static_cast<uint32_t>(0x12345678),
        std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12});

    // 测试思路：浮点也按固定宽度 numeric 处理。例：1.0f 的 IEEE754 大端
    // 表示为 3F 80 00 00；1.0 double 大端表示为 3F F0 后跟 6 个 0。
    ExpectRoundTrip<float>(
        MakeSpec(FieldType::kFloat, sizeof(float)),
        1.0f,
        std::vector<uint8_t>{0x3F, 0x80, 0x00, 0x00});
    ExpectRoundTrip<double>(
        MakeSpec(FieldType::kDouble, sizeof(double)),
        1.0,
        std::vector<uint8_t>{0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    // 测试思路：字符串是 raw bytes，不参与大小端反转。例："AB12" 应直接编码
    // 为 ASCII 字节 {0x41, 0x42, 0x31, 0x32}，解码后仍为同一字符串。
    ExpectRoundTrip<std::string>(
        MakeSpec(FieldType::kString, 4, FieldByteOrder::kRaw),
        std::string("AB12"),
        std::vector<uint8_t>{0x41, 0x42, 0x31, 0x32});

    // 测试思路：FieldSpec 的类型和 FieldScalar 的实际类型必须一致。例：
    // 字段声明为 uint16_t 时传入 uint32_t，应拒绝编码，避免悄悄截断。
    EXPECT_THROW(
        EncodeField(MakeSpec(FieldType::kUint16, sizeof(uint16_t)),
                    FieldScalar{static_cast<uint32_t>(0x1234)}),
        std::invalid_argument);

    // 测试思路：numeric 字段必须严格匹配固定宽度。例：uint16_t 只能是 2 字节，
    // spec 写成 4 字节或传入 1 字节数据都应失败。
    EXPECT_THROW(
        EncodeField(MakeSpec(FieldType::kUint16, 4),
                    FieldScalar{static_cast<uint16_t>(0x1234)}),
        std::invalid_argument);
    EXPECT_THROW(
        DecodeField(MakeSpec(FieldType::kUint16, sizeof(uint16_t)),
                    std::vector<uint8_t>{0x12}),
        std::invalid_argument);

    // 测试思路：numeric 字段不能使用 raw 端序。raw 只保留字符串等原始字节语义，
    // 否则长度字段和普通数值字段会失去明确的端序解释。
    EXPECT_THROW(
        EncodeField(MakeSpec(FieldType::kUint16, sizeof(uint16_t), FieldByteOrder::kRaw),
                    FieldScalar{static_cast<uint16_t>(0x1234)}),
        std::invalid_argument);
    EXPECT_THROW(
        DecodeField(MakeSpec(FieldType::kUint16, sizeof(uint16_t), FieldByteOrder::kRaw),
                    std::vector<uint8_t>{0x12, 0x34}),
        std::invalid_argument);
}

TEST(TestCustomTcpField, RoleBehaviorAndFieldValue)
{
    // 测试思路：role 行为表是 PatternSpec 校验、Item 覆盖和 serialize 分派的唯一入口。
    // 例：start_magic 必须要求 match，common 是初版唯一允许 Item fields 覆盖的字段。
    EXPECT_FALSE(IsRequiredUnique(FieldRole::kCommon));
    EXPECT_TRUE(IsRequiredUnique(FieldRole::kStartMagic));
    EXPECT_TRUE(IsRequiredUnique(FieldRole::kFunctionCode));

    EXPECT_FALSE(IsRequiresMatch(FieldRole::kCommon));
    EXPECT_TRUE(IsRequiresMatch(FieldRole::kStartMagic));
    EXPECT_FALSE(IsRequiresMatch(FieldRole::kFunctionCode));

    EXPECT_TRUE(IsItemOverrideAllowed(FieldRole::kCommon));
    EXPECT_FALSE(IsItemOverrideAllowed(FieldRole::kStartMagic));
    EXPECT_FALSE(IsItemOverrideAllowed(FieldRole::kFunctionCode));

    EXPECT_TRUE(IsAutoPatch(FieldRole::kBodyLength));
    EXPECT_TRUE(IsAutoPatch(FieldRole::kTotalLength));
    EXPECT_FALSE(IsAutoPatch(FieldRole::kCommon));

    EXPECT_EQ(WriteKindOf(FieldRole::kStartMagic), FieldWriteKind::kFixedMatch);
    EXPECT_EQ(WriteKindOf(FieldRole::kFunctionCode), FieldWriteKind::kItemFunctionCode);
    EXPECT_EQ(WriteKindOf(FieldRole::kCommon), FieldWriteKind::kItemFieldOverride);
    EXPECT_EQ(WriteKindOf(FieldRole::kBodyLength), FieldWriteKind::kAutoPatch);

    EXPECT_TRUE(IsReservedUnsupported(FieldRole::kEndMagic));
    EXPECT_TRUE(IsReservedUnsupported(FieldRole::kChecksum));
    EXPECT_FALSE(IsReservedUnsupported(FieldRole::kCommon));

    EXPECT_EQ(RoleTag(FieldRole::kStartMagic), "start_magic");
    EXPECT_EQ(RoleTag(FieldRole::kBodyLength), "body_length");

    // 测试思路：FieldValue::hex() 必须基于 wire bytes 稳定输出无分隔 H 前缀十六进制。
    // 例：bytes {0x12, 0x34} 应输出 H1234，而不是带空格的 H12 34。
    FieldSpec common_spec = MakeSpec(FieldType::kUint16, sizeof(uint16_t));
    common_spec.role = FieldRole::kCommon;
    FieldValue common_value{common_spec, std::vector<uint8_t>{0x12, 0x34}};
    EXPECT_EQ(common_value.hex(), "H1234");

    // 测试思路：FieldValue::hex() 是原始 bytes 展示接口，不能丢前导 0，
    // 也不能把 A-F 输出成小写。例：{0x00, 0x0F, 0xA0, 0xFF}
    // 应稳定展示为 H000FA0FF。
    FieldSpec hex_edge_spec = MakeSpec(FieldType::kUint32, sizeof(uint32_t));
    hex_edge_spec.role = FieldRole::kCommon;
    FieldValue hex_edge_value{
        hex_edge_spec, std::vector<uint8_t>{0x00, 0x0F, 0xA0, 0xFF}};
    EXPECT_EQ(hex_edge_value.hex(), "H000FA0FF");

    // 测试思路：FieldValue::scalar() 应通过 FieldSpec 解码为实际编译期类型。
    // 例：INT16 big endian bytes {0xFF, 0xFE} 对应 int16_t(-2)，
    // variant 中不能被误读成 uint16_t 或字符串。
    FieldSpec signed_i16_spec = MakeSpec(FieldType::kInt16, sizeof(int16_t));
    signed_i16_spec.role = FieldRole::kCommon;
    FieldValue signed_i16_value{signed_i16_spec, std::vector<uint8_t>{0xFF, 0xFE}};
    const auto signed_i16_scalar = signed_i16_value.scalar();
    const auto* signed_i16 = std::get_if<int16_t>(&signed_i16_scalar);
    ASSERT_NE(signed_i16, nullptr);
    EXPECT_EQ(*signed_i16, static_cast<int16_t>(-2));

    // 测试思路：FieldValue::scalar() 必须尊重字段端序，而不是直接按内存拷贝。
    // 例：UINT32 little endian wire bytes {78 56 34 12} 应解析为 0x12345678。
    FieldSpec scalar_u32_le_spec = MakeSpec(
        FieldType::kUint32, sizeof(uint32_t), FieldByteOrder::kLittleEndian);
    scalar_u32_le_spec.role = FieldRole::kCommon;
    FieldValue scalar_u32_le_value{
        scalar_u32_le_spec, std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12}};
    const auto scalar_u32_le = scalar_u32_le_value.scalar();
    const auto* u32_le = std::get_if<uint32_t>(&scalar_u32_le);
    ASSERT_NE(u32_le, nullptr);
    EXPECT_EQ(*u32_le, 0x12345678u);

    // 测试思路：字符串字段的 scalar() 是 raw bytes 到 string 的映射，
    // 中间的 0x00 不能被当成 C 字符串结束符截断。例：{'A', 0x00, 'B'}
    // 解码后应保留 3 字节内容。
    FieldSpec string_spec = MakeSpec(FieldType::kString, 3, FieldByteOrder::kRaw);
    string_spec.role = FieldRole::kCommon;
    FieldValue string_value{string_spec, std::vector<uint8_t>{0x41, 0x00, 0x42}};
    const auto string_scalar = string_value.scalar();
    const auto* string_decoded = std::get_if<std::string>(&string_scalar);
    ASSERT_NE(string_decoded, nullptr);
    EXPECT_EQ(*string_decoded, std::string("A\0B", 3));

    // 测试思路：FieldValue::scalar() 不应吞掉底层解码错误。例：UINT16
    // 只提供 1 字节 bytes 时长度不匹配，应抛出异常，避免后续业务拿到半截值。
    FieldValue short_u16_value{common_spec, std::vector<uint8_t>{0x12}};
    EXPECT_THROW(short_u16_value.scalar(), std::invalid_argument);

    // 测试思路：长度字段应按 FieldSpec 类型和端序解码 bytes，而不是返回 byte_len。
    // 例：UINT16 body_length bytes {0x12, 0x34} 应解析为 0x1234。
    FieldSpec body_len_u16 = MakeSpec(FieldType::kUint16, sizeof(uint16_t));
    body_len_u16.role = FieldRole::kBodyLength;
    FieldValue body_len_u16_value{body_len_u16, std::vector<uint8_t>{0x12, 0x34}};
    EXPECT_EQ(body_len_u16_value.asUnsignedLength(), 0x1234u);

    // 测试思路：UINT32 小端长度字段也要得到相同业务长度。例：wire bytes
    // {0x78, 0x56, 0x34, 0x12} 在 little endian 下应解析为 0x12345678。
    FieldSpec body_len_u32_le = MakeSpec(
        FieldType::kUint32, sizeof(uint32_t), FieldByteOrder::kLittleEndian);
    body_len_u32_le.role = FieldRole::kBodyLength;
    FieldValue body_len_u32_le_value{
        body_len_u32_le, std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12}};
    EXPECT_EQ(body_len_u32_le_value.asUnsignedLength(), 0x12345678u);

    // 测试思路：total_length 也是合法长度角色，正数有符号整型应允许转换为
    // 无符号长度。例：INT16 total_length bytes {0x00, 0x2A} 应得到 42。
    FieldSpec total_len_i16 = MakeSpec(FieldType::kInt16, sizeof(int16_t));
    total_len_i16.role = FieldRole::kTotalLength;
    FieldValue total_len_i16_value{total_len_i16, std::vector<uint8_t>{0x00, 0x2A}};
    EXPECT_EQ(total_len_i16_value.asUnsignedLength(), 42u);

    // 测试思路：asUnsignedLength() 返回 uint64_t，不能把 UINT64 大长度截断。
    // 例：8 字节全 FF 应解析为 uint64_t 最大值。
    FieldSpec total_len_u64 = MakeSpec(FieldType::kUint64, sizeof(uint64_t));
    total_len_u64.role = FieldRole::kTotalLength;
    FieldValue total_len_u64_value{
        total_len_u64,
        std::vector<uint8_t>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    EXPECT_EQ(total_len_u64_value.asUnsignedLength(), std::numeric_limits<uint64_t>::max());

    // 测试思路：asUnsignedLength() 只服务 body_length/total_length 角色。
    // 例：common 字段即便 bytes 可以解码为 UINT16(0x1234)，也不能被当作长度字段读取。
    EXPECT_THROW(common_value.asUnsignedLength(), std::invalid_argument);

    // 测试思路：长度字段不能接受负数、浮点或字符串。例：INT16(-1) 的 bytes 是
    // FF FF，但它不是合法无符号长度，应显式失败而不是返回 0 或巨大值。
    FieldSpec negative_len = MakeSpec(FieldType::kInt16, sizeof(int16_t));
    negative_len.role = FieldRole::kBodyLength;
    FieldValue negative_len_value{negative_len, std::vector<uint8_t>{0xFF, 0xFF}};
    EXPECT_THROW(negative_len_value.asUnsignedLength(), std::invalid_argument);

    FieldSpec float_len = MakeSpec(FieldType::kFloat, sizeof(float));
    float_len.role = FieldRole::kBodyLength;
    FieldValue float_len_value{float_len, std::vector<uint8_t>{0x3F, 0x80, 0x00, 0x00}};
    EXPECT_THROW(float_len_value.asUnsignedLength(), std::invalid_argument);

    FieldSpec string_len = MakeSpec(FieldType::kString, 2, FieldByteOrder::kRaw);
    string_len.role = FieldRole::kBodyLength;
    FieldValue string_len_value{string_len, std::vector<uint8_t>{0x31, 0x32}};
    EXPECT_THROW(string_len_value.asUnsignedLength(), std::invalid_argument);
}

TEST(TestCustomTcpField, FieldByteOrderStringConvert)
{
    // 测试思路：Pattern JSON 中 byte_order 使用稳定的小写字符串，domain 层
    // 需要能和 FieldByteOrder 枚举双向转换。例："big" 对应大端，
    // "little" 对应小端，"raw" 对应不做端序解释的原始字节。
    EXPECT_EQ(FieldByteOrderFromString("big"), FieldByteOrder::kBigEndian);
    EXPECT_EQ(FieldByteOrderFromString("little"), FieldByteOrder::kLittleEndian);
    EXPECT_EQ(FieldByteOrderFromString("raw"), FieldByteOrder::kRaw);

    EXPECT_EQ(FieldByteOrderToString(FieldByteOrder::kBigEndian), "big");
    EXPECT_EQ(FieldByteOrderToString(FieldByteOrder::kLittleEndian), "little");
    EXPECT_EQ(FieldByteOrderToString(FieldByteOrder::kRaw), "raw");

    // 测试思路：非法 byte_order 必须在配置解析阶段暴露，而不是默认为 big。
    // 例："middle" 没有协议语义，应直接拒绝。
    EXPECT_THROW(FieldByteOrderFromString("middle"), std::invalid_argument);
    EXPECT_THROW(FieldByteOrderFromString(""), std::invalid_argument);

    // 测试思路：如果后续枚举反序列化或脏数据产生未知枚举值，
    // ToString 也必须显式失败。例：999 不是任何合法 FieldByteOrder。
    EXPECT_THROW(FieldByteOrderToString(static_cast<FieldByteOrder>(999)), std::invalid_argument);
}
