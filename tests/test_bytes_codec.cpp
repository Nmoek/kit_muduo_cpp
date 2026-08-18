/**
 * @file test_bytes_codec.cpp
 * @brief bytes codec 测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "gtest/gtest.h"

#include "base/bytes_codec.h"

#include <string>
#include <vector>

using namespace kit_muduo;

namespace {

struct CodecJsonDto
{
    int32_t id{0};
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CodecJsonDto, id, name)
};

BytesView MakeBytesView(const std::string& data)
{
    return BytesView{
        .data = reinterpret_cast<const uint8_t*>(data.data()),
        .size = data.size(),
    };
}

} // namespace

/*
测试思路：
1. JsonCodec::Parse 只面对一段 BytesView，不关心 HTTP Content-Type。
2. 输入是合法 JSON object，解析后 nlohmann::json 应保留字段和值。
3. 该用例固定 base 层“纯格式能力”的边界。

示例：
  bytes={"id":7,"name":"kit"} -> json["id"] == 7
*/
TEST(BytesCodecTest, JsonParseAcceptsByteRange)
{
    const std::string body = R"({"id":7,"name":"kit"})";
    nlohmann::json root;

    const auto result = JsonCodec::Parse(MakeBytesView(body), root);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(root["id"], 7);
    EXPECT_EQ(root["name"], "kit");
}

/*
测试思路：
1. JsonCodec::Validate 用于轻量校验 JSON 语法，不绑定 DTO。
2. 非法 JSON 应返回失败，错误码要能表达“数据非法或解析失败”。
3. 调用方可以据此在 domain/http 层统一转换错误。

示例：
  bytes={"id": -> !ok
*/
TEST(BytesCodecTest, JsonValidateRejectsBrokenJson)
{
    const std::string body = R"({"id":)";

    const auto result = JsonCodec::Validate(MakeBytesView(body));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.code == CodecErrorCode::kInvalidData
        || result.code == CodecErrorCode::kDecodeFailed);
}

/*
测试思路：
1. JsonCodec::Decode<T> 在 JSON 语法正确后调用 nlohmann::json::get_to。
2. 字段类型不匹配时必须返回 kDecodeFailed。
3. 这覆盖 DTO decode 失败路径，而不是 JSON parse 失败路径。

示例：
  {"id":"not-int","name":"kit"} -> CodecJsonDto decode failed
*/
TEST(BytesCodecTest, JsonDecodeRejectsFieldTypeMismatch)
{
    const std::string body = R"({"id":"not-int","name":"kit"})";
    CodecJsonDto dto;

    const auto result = JsonCodec::Decode(MakeBytesView(body), dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, CodecErrorCode::kDecodeFailed);
}

/*
测试思路：
1. JsonCodec::Encode<T> 应能把 DTO 转成 JSON bytes。
2. 输出使用 std::vector<uint8_t>，不能依赖 C 字符串。
3. 再 parse 输出验证字段值，避免只检查字符串顺序。

示例：
  CodecJsonDto{id=9,name="encode"} -> bytes -> json["id"] == 9
*/
TEST(BytesCodecTest, JsonEncodeWritesBytes)
{
    CodecJsonDto dto;
    dto.id = 9;
    dto.name = "encode";
    std::vector<uint8_t> bytes;

    const auto result = JsonCodec::Encode(dto, bytes);

    ASSERT_TRUE(result.ok) << result.message;
    const nlohmann::json root = nlohmann::json::parse(bytes.begin(), bytes.end());
    EXPECT_EQ(root["id"], 9);
    EXPECT_EQ(root["name"], "encode");
}

/*
测试思路：
1. TextCodec::Decode 只是 bytes 到 string 的复制，不做 charset 判断。
2. 输入包含 NUL 字节时，输出 string 不能被截断。
3. out 原本有旧值，decode 前应被清空。

示例：
  out="old", bytes={'a','\0','b'} -> out.size()==3
*/
TEST(BytesCodecTest, TextDecodeClearsAndPreservesNulByte)
{
    const std::string body("a\0b", 3);
    std::string out = "old";

    const auto result = TextCodec::Decode(MakeBytesView(body), out);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(out.size(), 3u);
    EXPECT_EQ(out, body);
}

/*
测试思路：
1. XmlCodec 第一版只承诺结构校验。
2. 完整 XML 应成功，未闭合 XML 应失败。
3. 该用例避免 XML 校验退化成简单字符串判断。

示例：
  <root><a>1</a></root> -> ok
  <root><a></root>     -> !ok
*/
TEST(BytesCodecTest, XmlValidateAcceptsValidAndRejectsInvalid)
{
    const std::string valid = "<root><a>1</a></root>";
    const std::string invalid = "<root><a></root>";

    EXPECT_TRUE(XmlCodec::Validate(MakeBytesView(valid)).ok);
    const auto invalid_result = XmlCodec::Validate(MakeBytesView(invalid));
    EXPECT_FALSE(invalid_result.ok);
    EXPECT_EQ(invalid_result.code, CodecErrorCode::kInvalidData);
}


