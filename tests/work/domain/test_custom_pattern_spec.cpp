/**
 * @file test_custom_pattern_spec.cpp
 * @brief 自定义TCP协议格式定义测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-17 22:36:32
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../../test_log.h"
#include "gtest/gtest.h"

#include "domain/custom_tcp_field_model.h"
#include "domain/custom_tcp_pattern_spec.h"

#include <string>
#include <vector>

using namespace kit_domain;
using nljson = nlohmann::json;

namespace {

const std::string kValidPatternJson = R"({
  "version": 2,
  "header_bytes": 26,
  "default_order": "big",
  "length_policy": "body_length",
  "fields": [
    {"name":"起始标识","byte_pos":0,"byte_len":4,"type":"UINT32","role":"start_magic","match":"H23232323"},
    {"name":"消息总长度","byte_pos":4,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"消息序列号","byte_pos":8,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"功能码","byte_pos":12,"byte_len":2,"type":"UINT16","role":"function_code"},
    {"name":"报文体长度","byte_pos":14,"byte_len":4,"type":"UINT32","role":"body_length"},
    {"name":"消息时间戳","byte_pos":18,"byte_len":8,"type":"UINT64","role":"common"}
  ]
})";

nljson LoadValidPatternJson()
{
    return nljson::parse(kValidPatternJson);
}

} // namespace

TEST(TestCustomTcpPatternSpec, ParseAndSerializeRoundTrip)
{
    // 测试思路：合法 Pattern JSON 应该能完整解析成 PatternSpec，并且序列化回去后
    // 保持结构一致。例：start_magic 的 match 应还原为 H23232323 对应的 4 字节。
    const nljson root = LoadValidPatternJson();
    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    ASSERT_TRUE(pattern_spec_opt.has_value());

    const auto& spec = *pattern_spec_opt;
    EXPECT_EQ(spec.version, 2);
    EXPECT_EQ(spec.header_bytes, 26u);
    EXPECT_EQ(spec.default_order, FieldByteOrder::kBigEndian);
    EXPECT_EQ(spec.length_policy, LengthPolicy::kBodyLength);
    EXPECT_EQ(spec.fields.size(), 6u);

    const FieldSpec* start_magic = spec.byPos(0);
    ASSERT_NE(start_magic, nullptr);
    EXPECT_EQ(start_magic->name, "起始标识");
    EXPECT_EQ(start_magic->byte_pos, 0u);
    EXPECT_EQ(start_magic->byte_len, 4u);
    EXPECT_EQ(start_magic->type, FieldType::kUint32);
    EXPECT_EQ(start_magic->role, FieldRole::kStartMagic);
    EXPECT_EQ(start_magic->byte_order, FieldByteOrder::kBigEndian);
    ASSERT_TRUE(start_magic->match.has_value());
    EXPECT_EQ(*start_magic->match, std::vector<uint8_t>({0x23, 0x23, 0x23, 0x23}));

    const auto commons = spec.byUniqueRole(FieldRole::kCommon);
    ASSERT_EQ(commons, nullptr);

    const nljson serialized = spec;
    EXPECT_EQ(serialized, root);
}

TEST(TestCustomTcpPatternSpec, ParseRejectsInvalidVersion)
{
    // 测试思路：version 不是 2 时，PatternSpec 不应进入后续字段校验。
    // 例：version=1 说明仍是旧协议语义，应直接拒绝。
    nljson root = LoadValidPatternJson();
    root["version"] = 1;

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsHeaderBytesMismatch)
{
    // 测试思路：header_bytes 必须和字段布局实际占用长度一致。
    // 例：把 header_bytes 改小后，会导致字段总边界与声明不一致，应该失败。
    nljson root = LoadValidPatternJson();
    root["header_bytes"] = 25;

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsHeaderBytesTooLarge)
{
    // 测试思路：header_bytes 也不能大于字段定义实际占用长度。
    // 例：把 header_bytes 改大到 27，说明声明头长比字段总和多出 1 字节，应失败。
    nljson root = LoadValidPatternJson();
    root["header_bytes"] = 27;

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsOverlappedFields)
{
    // 测试思路：字段区间不能重叠。例：让功能码字段挪到 byte_pos=10，
    // 它会与消息序列号字段的区间重叠，解析应失败。
    nljson root = LoadValidPatternJson();
    root["fields"][3]["byte_pos"] = 10;

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsDuplicateBytePos)
{
    // 测试思路：byte_pos 是字段唯一索引。例：两个字段都占用 byte_pos=4，
    // 其中一个会在集合插入阶段被判定为重复。
    nljson root = LoadValidPatternJson();
    root["fields"][2]["byte_pos"] = 4;

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsMissingStartMagic)
{
    // 测试思路：start_magic 是协议头的基础匹配字段，必须唯一存在。
    // 例：把 start_magic 改成 common 并清空 match，validate 应在角色统计阶段拒绝。
    nljson root = LoadValidPatternJson();
    root["fields"][0]["role"] = "common";
    root["fields"][0].erase("match");

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsMissingFunctionCode)
{
    // 测试思路：function_code 是运行时查找协议项的关键索引字段，必须唯一存在。
    // 例：把功能码字段改成 common，Pattern 就不再具备运行期索引能力。
    nljson root = LoadValidPatternJson();
    root["fields"][3]["role"] = "common";

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsLengthPolicyConflict)
{
    // 测试思路：length_policy 和字段角色集合必须一致。
    // 例：当 length_policy=no_length 时，Pattern 中不应再出现 body_length 或 total_length。
    nljson root = LoadValidPatternJson();
    root["length_policy"] = "no_length";

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsInvalidMatchLength)
{
    // 测试思路：start_magic 的 match 长度必须严格等于 byte_len。
    // 例：4 字节字段只给 2 字节 match，说明配置缺失或截断，必须失败。
    nljson root = LoadValidPatternJson();
    root["fields"][0]["match"] = "H2323";

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsUnsupportedRole)
{
    // 测试思路：end_magic/checksum 初版是保留但不支持的角色。
    // 例：把普通字段改成 checksum 后，validate 应直接拒绝。
    nljson root = LoadValidPatternJson();
    root["fields"][5]["role"] = "checksum";

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsStringFieldWithZeroLength)
{
    // 测试思路：STR 字段不能是 0 长度，否则 JSON 里虽然有类型但没有可编码载体。
    // 例：把时间戳改成字符串并把 byte_len 设为 0，应被 FieldSpec::validate 拒绝。
    nljson root = LoadValidPatternJson();
    root["fields"][5]["type"] = "STR";
    root["fields"][5]["byte_len"] = 0;

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}

TEST(TestCustomTcpPatternSpec, ParseRejectsNumericFieldLengthMismatch)
{
    // 测试思路：numeric 类型的 byte_len 必须和具体类型宽度严格一致。
    // 例：把消息总长度改成 UINT64 但仍保持 4 字节，说明字段定义无法完整表达数值，应拒绝。
    nljson root = LoadValidPatternJson();
    root["fields"][1]["type"] = "UINT64";

    auto pattern_spec_opt = CustomTcpPatternSpec::FromJson(root);
    EXPECT_FALSE(pattern_spec_opt.has_value());
}
