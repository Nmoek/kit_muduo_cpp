/**
 * @file test_custom_pattern.cpp
 * @brief 自定义TCP协议格式测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-17 22:36:32
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../../test_log.h"
#include "gtest/gtest.h"

#include "domain/custom_tcp_pattern_spec.h"
#include "domain/custom_tcp_pattern.h"
#include "net/net_data_converter.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kit_domain;
using nljson = nlohmann::json;

namespace {

// 策略 "body_length" 的测试数据。
const std::string kPatternJsonStr1 = R"({
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

const std::string kRespBody1 = R"({"msg":"hello world"})";

// 由 test_custom_project_server.h 中的 pattern_json_str2_1 转换而来：
// total_length_field -> role=total_length，旧 req/resp cfg 中的其它头字段转为 common。
// 按广州协议文档，所有数值字段采用 little-endian。
const std::string kPatternJsonStr2_1 = R"({
  "version": 2,
  "header_bytes": 26,
  "default_order": "little",
  "length_policy": "total_length",
  "fields": [
    {"name":"起始标识","byte_pos":0,"byte_len":4,"type":"UINT32","role":"start_magic","match":"H23232323"},
    {"name":"报文总长度","byte_pos":4,"byte_len":4,"type":"UINT32","role":"total_length"},
    {"name":"消息序列号","byte_pos":8,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"功能码","byte_pos":12,"byte_len":2,"type":"UINT16","role":"function_code"},
    {"name":"报文体长度","byte_pos":14,"byte_len":4,"type":"UINT32","role":"common"},
    {"name":"消息时间戳","byte_pos":18,"byte_len":8,"type":"UINT64","role":"common"}
  ]
})";

const std::string kRespBody2_1 = R"({"msg":"hello world"})";

// 由 test_custom_project_server.h 中的 pattern_json_str2_2 转换而来：
// 郑州邮政格式按“二进制头部 + 二进制 body”建模：
// header 只有起始字符、报文总长度、功能码 3 个字段；长度字段表示整包总长度。
const std::string kPatternJsonStr2_2 = R"({
  "version": 2,
  "header_bytes": 4,
  "default_order": "big",
  "length_policy": "total_length",
  "fields": [
    {"name":"起始字符","byte_pos":0,"byte_len":1,"type":"INT8","role":"start_magic","match":"H02"},
    {"name":"报文总长度","byte_pos":1,"byte_len":2,"type":"UINT16","role":"total_length"},
    {"name":"功能码","byte_pos":3,"byte_len":1,"type":"INT8","role":"function_code"}
  ]
})";

// 由 test_custom_project_server.h 中的 pattern_json_str3 转换而来：
// 策略 "no_length" 的测试数据需要覆盖完整可序列化头。
const std::string kPatternJsonStr3 = R"({
  "version": 2,
  "header_bytes": 24,
  "default_order": "raw",
  "length_policy": "no_length",
  "fields": [
    {"name":"起始字符","byte_pos":0,"byte_len":2,"type":"STR","role":"start_magic","match":"H023A"},
    {"name":"功能码","byte_pos":2,"byte_len":2,"type":"STR","role":"function_code"},
    {"name":"分隔符","byte_pos":4,"byte_len":1,"type":"STR","role":"common"},
    {"name":"设备类型","byte_pos":5,"byte_len":2,"type":"STR","role":"common"},
    {"name":"分隔符","byte_pos":7,"byte_len":1,"type":"STR","role":"common"},
    {"name":"安检机站号","byte_pos":8,"byte_len":2,"type":"STR","role":"common"},
    {"name":"分隔符","byte_pos":10,"byte_len":1,"type":"STR","role":"common"},
    {"name":"心跳序号","byte_pos":11,"byte_len":10,"type":"STR","role":"common"},
    {"name":"分隔符","byte_pos":21,"byte_len":1,"type":"STR","role":"common"},
    {"name":"结束魔数字","byte_pos":22,"byte_len":2,"type":"STR","role":"common"}
  ]
})";

BodyLengthPattern MakePatternJsonStr1Pattern()
{
    auto spec_opt = CustomTcpPatternSpec::FromJson(nljson::parse(kPatternJsonStr1));
    if(!spec_opt.has_value())
    {
        ADD_FAILURE() << "failed to parse kPatternJsonStr1";
        throw std::invalid_argument("kPatternJsonStr1 invalid!");
    }
    return BodyLengthPattern(spec_opt.value());
}

TotalLengthPattern MakePatternJsonStr2_1Pattern()
{
    auto spec_opt = CustomTcpPatternSpec::FromJson(nljson::parse(kPatternJsonStr2_1));
    if(!spec_opt.has_value())
    {
        ADD_FAILURE() << "failed to parse kPatternJsonStr2_1";
        throw std::invalid_argument("kPatternJsonStr2_1 invalid!");
    }
    return TotalLengthPattern(spec_opt.value());
}

TotalLengthPattern MakePatternJsonStr2_2Pattern()
{
    auto spec_opt = CustomTcpPatternSpec::FromJson(nljson::parse(kPatternJsonStr2_2));
    if(!spec_opt.has_value())
    {
        ADD_FAILURE() << "failed to parse kPatternJsonStr2_2";
        throw std::invalid_argument("kPatternJsonStr2_2 invalid!");
    }
    return TotalLengthPattern(spec_opt.value());
}

NoLengthPattern MakePatternJsonStr3Pattern()
{
    auto spec_opt = CustomTcpPatternSpec::FromJson(nljson::parse(kPatternJsonStr3));
    if(!spec_opt.has_value())
    {
        ADD_FAILURE() << "failed to parse kPatternJsonStr3";
        throw std::invalid_argument("kPatternJsonStr3 invalid!");
    }
    return NoLengthPattern(spec_opt.value());
}

std::vector<uint8_t> BytesOf(const std::string& data)
{
    return std::vector<uint8_t>(data.begin(), data.end());
}

std::vector<uint8_t> HexBytes(const std::string& hex)
{
    return kit_muduo::HexStringToBytes(hex);
}

void AppendBytes(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

const FieldValue* FieldByPos(const std::vector<FieldValue>& fields, size_t byte_pos)
{
    auto it = std::find_if(fields.begin(), fields.end(), [byte_pos](const FieldValue& v) {
        return v.spec.byte_pos == byte_pos;
    });
    return it == fields.end() ? nullptr : &(*it);
}

void ExpectParsedFieldHex(const CustomTcpPattern::ParseHeaderResult& result,
                          size_t byte_pos,
                          const std::string& expected_hex)
{
    const FieldValue* field = FieldByPos(result.fields_value, byte_pos);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->hex(), expected_hex);
}

CustomTcpItemCfg MakeReqCfg1()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H0100";

    // 由旧 req_cfg1 转换：
    // H0209 -> H00000209，H0003 -> H00000003，空时间戳 -> 8 字节 0。
    cfg.field_values_by_byte_pos.emplace(
        4, std::vector<uint8_t>{0x00, 0x00, 0x02, 0x09});
    cfg.field_values_by_byte_pos.emplace(
        8, std::vector<uint8_t>{0x00, 0x00, 0x00, 0x03});
    cfg.field_values_by_byte_pos.emplace(
        18, std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    return cfg;
}

CustomTcpItemCfg MakeRespCfg1()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H1080";

    // 由旧 resp_cfg1 转换： 中 common 字段值必须严格等于字段 byte_len。
    cfg.field_values_by_byte_pos.emplace(
        4, std::vector<uint8_t>{0x02, 0x09, 0x00, 0x00});
    cfg.field_values_by_byte_pos.emplace(
        8, std::vector<uint8_t>{0x00, 0x00, 0x00, 0x03});
    cfg.field_values_by_byte_pos.emplace(
        18, std::vector<uint8_t>{0xA8, 0x6D, 0x9F, 0x9F, 0x9A, 0x01, 0x00, 0x00});

    return cfg;
}

CustomTcpItemCfg MakeReqCfg2_1()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H0100";

    // 由旧 req_cfg2_1 转换：
    // 广州协议要求 little-endian，序列号、报文体长度和时间戳都按小端字节序写入。
    cfg.field_values_by_byte_pos.emplace(
        8, std::vector<uint8_t>{0x03, 0x00, 0x00, 0x00});
    cfg.field_values_by_byte_pos.emplace(
        14, std::vector<uint8_t>{0x0F, 0x00, 0x00, 0x00});
    cfg.field_values_by_byte_pos.emplace(
        18, std::vector<uint8_t>{0xA8, 0x6D, 0x9F, 0x9F, 0x9A, 0x01, 0x00, 0x00});

    return cfg;
}

CustomTcpItemCfg MakeRespCfg2_1()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H0180";

    // 由旧 resp_cfg2_1 转换：
    // 设备认证响应的消息类型应为 0x01,0x80；其余数值字段同样按 little-endian 写入。
    cfg.field_values_by_byte_pos.emplace(
        8, std::vector<uint8_t>{0x03, 0x00, 0x00, 0x00});
    cfg.field_values_by_byte_pos.emplace(
        14, std::vector<uint8_t>{0x0F, 0x00, 0x00, 0x00});
    cfg.field_values_by_byte_pos.emplace(
        18, std::vector<uint8_t>{0xA8, 0x6D, 0x9F, 0x9F, 0x9A, 0x01, 0x00, 0x00});

    return cfg;
}

CustomTcpItemCfg MakeReqCfg2_2()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H32";
    return cfg;
}

CustomTcpItemCfg MakeRespCfg2_2()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H42";
    return cfg;
}

std::vector<uint8_t> MakeReqBody2_2()
{
    // 由旧 ReqBuilderHelper2_2 的 header 之后字段转换：
    // 线体号 UINT16=1、包裹号 UINT32=9、LCR=1、结束字符=03 0D 0A。
    return HexBytes("H00010000000901030D0A");
}

std::vector<uint8_t> MakeRespBody2_2()
{
    // 由旧 MakeResp2_2 转换：
    // 线体号 UINT16=1、包裹号 UINT32=9、包裹ID UINT32=1、判图结果 UINT32=1(安全)、LCR=1、结束字符=03 0D 0A。
    return HexBytes("H000100000009000000010000000101030D0A");
}

CustomTcpItemCfg MakeReqCfg3()
{
    CustomTcpItemCfg cfg;
    cfg.function_code = "H3031";

    // 由旧 req_cfg3 转换：no_length 策略没有长度字段，完整头由 common 字段补齐。
    cfg.field_values_by_byte_pos.emplace(4, HexBytes("H7C"));
    cfg.field_values_by_byte_pos.emplace(5, HexBytes("H3031"));
    cfg.field_values_by_byte_pos.emplace(7, HexBytes("H7C"));
    cfg.field_values_by_byte_pos.emplace(8, HexBytes("H3031"));
    cfg.field_values_by_byte_pos.emplace(10, HexBytes("H7C"));
    cfg.field_values_by_byte_pos.emplace(11, HexBytes("H30303030303030303031"));
    cfg.field_values_by_byte_pos.emplace(21, HexBytes("H7C"));
    cfg.field_values_by_byte_pos.emplace(22, HexBytes("H0D0A"));

    return cfg;
}

CustomTcpItemCfg MakeRespCfg3()
{
    CustomTcpItemCfg cfg = MakeReqCfg3();
    cfg.function_code = "H3131";
    return cfg;
}

CustomTcpPatternSpec MakeUint8BodyLengthSpec()
{
    const std::string json = R"({
      "version": 2,
      "header_bytes": 7,
      "default_order": "big",
      "length_policy": "body_length",
      "fields": [
        {"name":"起始标识","byte_pos":0,"byte_len":4,"type":"UINT32","role":"start_magic","match":"H23232323"},
        {"name":"功能码","byte_pos":4,"byte_len":2,"type":"UINT16","role":"function_code"},
        {"name":"报文体长度","byte_pos":6,"byte_len":1,"type":"UINT8","role":"body_length"}
      ]
    })";

    auto spec_opt = CustomTcpPatternSpec::FromJson(nljson::parse(json));
    if(!spec_opt.has_value())
    {
        ADD_FAILURE() << "failed to parse uint8 body length spec";
        return CustomTcpPatternSpec{};
    }
    return *spec_opt;
}

} // namespace

/*
测试思路：
  旧 pattern_json_str1 是“头中有 body 长度”的格式， 解析时应按 byte_pos
  提取完整 26 字节头，再校验起始魔数、功能码和 body_length。

示意：
  H23232323 | total | seq | H0100 | body_len | timestamp | body
      ok    | common|common| func  |    15    |  common   | {"key1":"val1"}

举例：
  req_cfg1 的功能码 H0100 应被解析为 result.function_code，
  body 长度字段应自动回写为 H0000000F。
*/
TEST(TestCustomTcpPattern, ParseHeaderWithConvertedPatternJsonStr1AndReqCfg1)
{
    auto pattern = MakePatternJsonStr1Pattern();
    const std::vector<uint8_t> req_body = BytesOf(R"({"key1":"val1"})");

    auto serialized = pattern.serialize(MakeReqCfg1(), req_body);
    ASSERT_TRUE(serialized.has_value());

    auto result = pattern.parseHeader(*serialized);
    ASSERT_TRUE(result.ok()) << "status=" << result.status;
    EXPECT_EQ(result.function_code, "H0100");
    EXPECT_EQ(result.remain_body_bytes, req_body.size());
    ASSERT_EQ(result.fields_value.size(), 6u);

    ExpectParsedFieldHex(result, 0, "H23232323");
    ExpectParsedFieldHex(result, 4, "H00000209");
    ExpectParsedFieldHex(result, 8, "H00000003");
    ExpectParsedFieldHex(result, 12, "H0100");
    ExpectParsedFieldHex(result, 14, "H0000000F");
    ExpectParsedFieldHex(result, 18, "H0000000000000000");
}

/*
测试思路：
  resp_cfg1 + resp_body1 用于验证  serialize 的完整输出：固定魔数来自
  PatternSpec，功能码来自 ItemCfg，body_length 由 body 字节数自动回写，body 原样追加。

示意：
  PatternSpec(start_magic) + resp_cfg1(common/function_code) + len(resp_body1)
        |
        v
  26 字节 header + {"msg":"hello world"}

举例：
  resp_body1 长度为 21 字节，所以 body_length 字段应输出 H00000015。
*/
TEST(TestCustomTcpPattern, SerializeRespCfg1AndRespBody1PatchesBodyLength)
{
    auto pattern = MakePatternJsonStr1Pattern();
    const std::vector<uint8_t> resp_body = BytesOf(kRespBody1);
    ASSERT_EQ(resp_body.size(), 21u);

    auto serialized = pattern.serialize(MakeRespCfg1(), resp_body);
    ASSERT_TRUE(serialized.has_value());

    std::vector<uint8_t> expected{
        0x23, 0x23, 0x23, 0x23,
        0x02, 0x09, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x03,
        0x10, 0x80,
        0x00, 0x00, 0x00, 0x15,
        0xA8, 0x6D, 0x9F, 0x9F, 0x9A, 0x01, 0x00, 0x00,
    };
    AppendBytes(expected, resp_body);

    EXPECT_EQ(*serialized, expected);

    auto result = pattern.parseHeader(*serialized);
    ASSERT_TRUE(result.ok()) << "status=" << result.status;
    EXPECT_EQ(result.function_code, "H1080");
    EXPECT_EQ(result.remain_body_bytes, resp_body.size());
    ExpectParsedFieldHex(result, 14, "H00000015");
}

/*
测试思路：
  旧 pattern_json_str2_1 是“头中有报文总长度”的格式。广州协议文档明确要求
  所有数值字段采用 little-endian，因此  的 default_order 也必须改成 little。
  serialize 时 total_length 仍由策略按 header_bytes + body.size() 自动回写。

示意：
  H23232323 | total_length(29 00 00 00) | seq(03 00 00 00) | H0100 | body_len(0F 00 00 00) | timestamp | body
      ok    |         26 + 15         |        3         | func  |          15           |  common   | {"key2":"val2"}

举例：
  req_cfg2_1 的功能码 H0100 应被解析出来；body 长度为 15 字节时，
  报文总长度字段应自动写成 H29000000。
*/
TEST(TestCustomTcpPattern, ParseHeaderWithConvertedPatternJsonStr2_1AndReqCfg2_1)
{
    auto pattern = MakePatternJsonStr2_1Pattern();
    const std::vector<uint8_t> req_body = BytesOf(R"({"key2":"val2"})");
    ASSERT_EQ(req_body.size(), 15u);

    auto serialized = pattern.serialize(MakeReqCfg2_1(), req_body);
    ASSERT_TRUE(serialized.has_value());

    auto result = pattern.parseHeader(*serialized);
    ASSERT_TRUE(result.ok()) << "status=" << result.status;
    EXPECT_EQ(result.function_code, "H0100");
    EXPECT_EQ(result.remain_body_bytes, req_body.size());
    ASSERT_EQ(result.fields_value.size(), 6u);

    ExpectParsedFieldHex(result, 0, "H23232323");
    ExpectParsedFieldHex(result, 4, "H29000000");
    ExpectParsedFieldHex(result, 8, "H03000000");
    ExpectParsedFieldHex(result, 12, "H0100");
    ExpectParsedFieldHex(result, 14, "H0F000000");
    ExpectParsedFieldHex(result, 18, "HA86D9F9F9A010000");
}

/*
测试思路：
  resp_cfg2_1 + resp_body2_1 用于验证 total_length 响应序列化：
  设备认证响应消息类型应为 0x01,0x80，且 total_length 由实际 body 字节数回写。

示意：
  PatternSpec(start_magic) + resp_cfg2_1(common/function_code) + len(resp_body2_1)
        |
        v
  total_length = 26 + 21 = H2F000000

举例：
  resp_body2_1 为 {"msg":"hello world"}，长度 21；最终 header[4,8) 应为 H2F000000。
*/
TEST(TestCustomTcpPattern, SerializeRespCfg2_1AndRespBody2_1PatchesTotalLength)
{
    auto pattern = MakePatternJsonStr2_1Pattern();
    const std::vector<uint8_t> resp_body = BytesOf(kRespBody2_1);
    ASSERT_EQ(resp_body.size(), 21u);

    auto serialized = pattern.serialize(MakeRespCfg2_1(), resp_body);
    ASSERT_TRUE(serialized.has_value());

    std::vector<uint8_t> expected{
        0x23, 0x23, 0x23, 0x23,
        0x2F, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00,
        0x01, 0x80,
        0x0F, 0x00, 0x00, 0x00,
        0xA8, 0x6D, 0x9F, 0x9F, 0x9A, 0x01, 0x00, 0x00,
    };
    AppendBytes(expected, resp_body);

    EXPECT_EQ(*serialized, expected);

    auto result = pattern.parseHeader(*serialized);
    ASSERT_TRUE(result.ok()) << "status=" << result.status;
    EXPECT_EQ(result.function_code, "H0180");
    EXPECT_EQ(result.remain_body_bytes, resp_body.size());
    ExpectParsedFieldHex(result, 4, "H2F000000");
}

/*
测试思路：
  旧 pattern_json_str2_2 是郑州邮政的 4 字节二进制头格式。修正后的协议文档中
  报文长度表示整包总长度，和  total_length = header_bytes + body.size() 一致。
  文档响应还包含“判图结果”字段，因此  测试要把这个字段补齐到 body。

示意：
  请求: H02 | total=4+10 | H32 | 0001 00000009 01 030D0A
  响应: H02 | total=4+18 | H42 | 0001 00000009 00000001 00000001 01 030D0A

举例：
  req_cfg2_2 没有字段覆盖值，只提供功能码 H32；serialize 应自动写总长度 H000E，
  parseHeader 应返回 remain_body_bytes=10。响应补齐判图结果后，body 为 18 字节，
  序列化后的整包总长度为 4 + 18 = 22，即 H0016。
*/
TEST(TestCustomTcpPattern, SerializeAndParseConvertedPatternJsonStr2_2TotalLength)
{
    auto pattern = MakePatternJsonStr2_2Pattern();

    const std::vector<uint8_t> req_body = MakeReqBody2_2();
    ASSERT_EQ(req_body.size(), 10u);

    auto req_serialized = pattern.serialize(MakeReqCfg2_2(), req_body);
    ASSERT_TRUE(req_serialized.has_value());
    EXPECT_EQ(*req_serialized, HexBytes("H02000E32" "00010000000901030D0A"));

    auto req_result = pattern.parseHeader(*req_serialized);
    ASSERT_TRUE(req_result.ok()) << "status=" << req_result.status;
    EXPECT_EQ(req_result.function_code, "H32");
    EXPECT_EQ(req_result.remain_body_bytes, req_body.size());
    ASSERT_EQ(req_result.fields_value.size(), 3u);
    ExpectParsedFieldHex(req_result, 0, "H02");
    ExpectParsedFieldHex(req_result, 1, "H000E");
    ExpectParsedFieldHex(req_result, 3, "H32");

    const std::vector<uint8_t> resp_body = MakeRespBody2_2();
    ASSERT_EQ(resp_body.size(), 18u);

    auto resp_serialized = pattern.serialize(MakeRespCfg2_2(), resp_body);
    ASSERT_TRUE(resp_serialized.has_value());
    EXPECT_EQ(*resp_serialized, HexBytes("H02001642" "000100000009000000010000000101030D0A"));

    auto resp_result = pattern.parseHeader(*resp_serialized);
    ASSERT_TRUE(resp_result.ok()) << "status=" << resp_result.status;
    EXPECT_EQ(resp_result.function_code, "H42");
    EXPECT_EQ(resp_result.remain_body_bytes, resp_body.size());
    ExpectParsedFieldHex(resp_result, 1, "H0016");
    ExpectParsedFieldHex(resp_result, 3, "H42");
}

/*
测试思路：
  旧 pattern_json_str3 没有长度字段。 中需要把旧 req_cfg3/resp_cfg3 的普通头字段
  一起纳入 PatternSpec，no_length 策略解析时 remain_body_bytes 固定为 0。

示意：
  H023A | func | 7C | 3031 | 7C | 3031 | 7C | heartbeat | 7C | 0D0A
    ok  | H3031/H3131
        |
        v
  remain_body_bytes = 0

举例：
  请求功能码 H3031 和响应功能码 H3131 应只改变 byte_pos=2 的 2 字节，其余 common
  字段保持相同，序列化结果正好是 24 字节完整头。
*/
TEST(TestCustomTcpPattern, SerializeAndParseConvertedPatternJsonStr3NoLength)
{
    auto pattern = MakePatternJsonStr3Pattern();

    auto req_serialized = pattern.serialize(MakeReqCfg3(), {});
    ASSERT_TRUE(req_serialized.has_value());
    EXPECT_EQ(*req_serialized, HexBytes(
        "H023A30317C30317C30317C"
        "30303030303030303031"
        "7C0D0A"));

    auto req_result = pattern.parseHeader(*req_serialized);
    ASSERT_TRUE(req_result.ok()) << "status=" << req_result.status;
    EXPECT_EQ(req_result.function_code, "H3031");
    EXPECT_EQ(req_result.remain_body_bytes, 0u);
    ASSERT_EQ(req_result.fields_value.size(), 10u);
    ExpectParsedFieldHex(req_result, 0, "H023A");
    ExpectParsedFieldHex(req_result, 2, "H3031");
    ExpectParsedFieldHex(req_result, 11, "H30303030303030303031");
    ExpectParsedFieldHex(req_result, 22, "H0D0A");

    auto resp_serialized = pattern.serialize(MakeRespCfg3(), {});
    ASSERT_TRUE(resp_serialized.has_value());
    EXPECT_EQ(*resp_serialized, HexBytes(
        "H023A31317C30317C30317C"
        "30303030303030303031"
        "7C0D0A"));

    auto resp_result = pattern.parseHeader(*resp_serialized);
    ASSERT_TRUE(resp_result.ok()) << "status=" << resp_result.status;
    EXPECT_EQ(resp_result.function_code, "H3131");
    EXPECT_EQ(resp_result.remain_body_bytes, 0u);
    ExpectParsedFieldHex(resp_result, 2, "H3131");
}

/*
测试思路：
  parseHeader 只负责解析协议头；当输入不足 header_bytes 时，不能尝试读取任何字段，
  应直接返回 kNonMinLength，让上层继续等待网络数据。

示意：
  [25 bytes]  <  header_bytes(26)
        |
        v
  kNonMinLength

举例：
  pattern_json_str1 声明 26 字节头，只给 25 字节时 fields_value 应为空。
*/
TEST(TestCustomTcpPattern, ParseHeaderRejectsDataShorterThanHeader)
{
    auto pattern = MakePatternJsonStr1Pattern();
    std::vector<uint8_t> short_data(pattern.spec().header_bytes - 1, 0x00);

    auto result = pattern.parseHeader(short_data);
    EXPECT_EQ(result.status, CustomTcpPattern::ParseHeaderResult::kNonMinLength);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.fields_value.empty());
    EXPECT_TRUE(result.function_code.empty());
    EXPECT_EQ(result.remain_body_bytes, 0u);
}

/*
测试思路：
  起始魔数是格式级固定匹配字段。即使其它字段长度都能提取，只要魔数不等于
  PatternSpec.match，就必须拒绝该报文。

示意：
  H24232323 | ...  !=  H23232323
        |
        v
  kInvalidDataError

举例：
  将合法请求第 1 字节从 0x23 改为 0x24，应命中魔数错误而不是继续匹配功能码。
*/
TEST(TestCustomTcpPattern, ParseHeaderRejectsStartMagicMismatch)
{
    auto pattern = MakePatternJsonStr1Pattern();
    auto serialized = pattern.serialize(MakeReqCfg1(), BytesOf(R"({"key1":"val1"})"));
    ASSERT_TRUE(serialized.has_value());
    (*serialized)[0] = 0x24;

    auto result = pattern.parseHeader(*serialized);
    EXPECT_EQ(result.status, CustomTcpPattern::ParseHeaderResult::kInvalidDataError);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.function_code.empty());
}

/*
测试思路：
  common 字段在  serialize 阶段允许只配置部分字段。PatternSpec 中声明了
  byte_pos=18 的 8 字节时间戳，如果 ItemCfg 没有提供对应值，应保留头部初始化的 0。

示意：
  field_values_by_byte_pos: {4, 8}  缺少 18
        |
        v
  serialize -> success，timestamp = H0000000000000000

举例：
  缺少“消息时间戳”时，功能码和 body_length 仍应写入，时间戳字段保持 8 字节 0。
*/
TEST(TestCustomTcpPattern, SerializeAllowsPartialCommonFieldOverrides)
{
    auto pattern = MakePatternJsonStr1Pattern();
    CustomTcpItemCfg cfg = MakeReqCfg1();
    cfg.field_values_by_byte_pos.erase(18);

    auto serialized = pattern.serialize(cfg, BytesOf(R"({"key1":"val1"})"));
    ASSERT_TRUE(serialized.has_value());

    auto result = pattern.parseHeader(*serialized);
    ASSERT_TRUE(result.ok()) << "status=" << result.status;
    EXPECT_EQ(result.function_code, "H0100");
    EXPECT_EQ(result.remain_body_bytes, BytesOf(R"({"key1":"val1"})").size());
    ExpectParsedFieldHex(result, 4, "H00000209");
    ExpectParsedFieldHex(result, 8, "H00000003");
    ExpectParsedFieldHex(result, 12, "H0100");
    ExpectParsedFieldHex(result, 14, "H0000000F");
    ExpectParsedFieldHex(result, 18, "H0000000000000000");
}

/*
测试思路：
  ItemCfg 中的字段覆盖值必须和 PatternSpec.byte_len 严格一致。字段长度错误时，
  不能截断、填充或错位写入。

示意：
  seq(byte_len=4) <- H0003(2 bytes)
        |
        v
  serialize -> nullopt

举例：
  把“消息序列号”从 4 字节改成 2 字节，序列化应失败。
*/
TEST(TestCustomTcpPattern, SerializeRejectsCommonFieldLengthMismatch)
{
    auto pattern = MakePatternJsonStr1Pattern();
    CustomTcpItemCfg cfg = MakeReqCfg1();
    cfg.field_values_by_byte_pos[8] = std::vector<uint8_t>{0x00, 0x03};

    auto serialized = pattern.serialize(cfg, BytesOf(R"({"key1":"val1"})"));
    EXPECT_FALSE(serialized.has_value());
}

/*
测试思路：
  功能码字段同样必须满足 byte_len。 不应接受短功能码或不带 H 前缀的伪十六进制值，
  否则运行期按功能码索引协议项会出现歧义。

示意：
  function_code byte_len=2
      H01   -> 1 byte  -> fail
      0100  -> no H    -> fail

举例：
  pattern_json_str1 的功能码是 2 字节，H01 和 0100 都不能被写入。
*/
TEST(TestCustomTcpPattern, SerializeRejectsInvalidFunctionCodeHex)
{
    auto pattern = MakePatternJsonStr1Pattern();
    CustomTcpItemCfg cfg = MakeReqCfg1();

    cfg.function_code = "H01";
    EXPECT_FALSE(pattern.serialize(cfg, BytesOf(R"({"key1":"val1"})")).has_value());

    cfg.function_code = "0100";
    EXPECT_FALSE(pattern.serialize(cfg, BytesOf(R"({"key1":"val1"})")).has_value());
}

/*
测试思路：
  body_length 由 body_data.size() 自动回写。空 body 是合法边界，应输出长度 0，
  且 parseHeader 返回 remain_body_bytes=0。

示意：
  body = []
        |
        v
  body_length = H00000000，输出大小 = header_bytes

举例：
  resp_cfg1 没有 body 时，最终报文只包含 26 字节头。
*/
TEST(TestCustomTcpPattern, SerializeAllowsEmptyBodyAndPatchesZeroLength)
{
    auto pattern = MakePatternJsonStr1Pattern();
    const std::vector<uint8_t> empty_body;

    auto serialized = pattern.serialize(MakeRespCfg1(), empty_body);
    ASSERT_TRUE(serialized.has_value());
    ASSERT_EQ(serialized->size(), pattern.spec().header_bytes);

    auto result = pattern.parseHeader(*serialized);
    ASSERT_TRUE(result.ok()) << "status=" << result.status;
    EXPECT_EQ(result.function_code, "H1080");
    EXPECT_EQ(result.remain_body_bytes, 0u);
    ExpectParsedFieldHex(result, 14, "H00000000");
}

/*
测试思路：
  自动长度回写必须检查字段类型上限。UINT8 body_length 最多只能表达 255 字节，
  255 应成功，256 应失败，避免长度字段溢出后协议头和真实 body 不一致。

示意：
  UINT8 length:
      255 -> HFF
      256 -> overflow -> nullopt

举例：
  构造 7 字节最小头，body 为 255 字节时第 6 字节应为 0xFF；
  body 为 256 字节时不能序列化。
*/
TEST(TestCustomTcpPattern, SerializeChecksBodyLengthFieldOverflow)
{
    BodyLengthPattern pattern(MakeUint8BodyLengthSpec());
    CustomTcpItemCfg cfg;
    cfg.function_code = "H0100";

    std::vector<uint8_t> max_body(255, 0xAA);
    auto max_serialized = pattern.serialize(cfg, max_body);
    ASSERT_TRUE(max_serialized.has_value());
    ASSERT_EQ(max_serialized->size(), pattern.spec().header_bytes + max_body.size());
    EXPECT_EQ((*max_serialized)[6], 0xFF);

    std::vector<uint8_t> overflow_body(256, 0xAA);
    auto overflow_serialized = pattern.serialize(cfg, overflow_body);
    EXPECT_FALSE(overflow_serialized.has_value());
}
