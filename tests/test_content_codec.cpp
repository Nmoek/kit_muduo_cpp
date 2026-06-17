/**
 * @file test_content_codec.cpp
 * @brief 基础数据格式序列化/反序列化测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-15 16:39:01
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "./test_log.h"
#include "gtest/gtest.h"
#include <exception>
#include "base/content_codec.h"

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

struct CodecMultipartDto
{
    CodecJsonDto header;
    nlohmann::json raw_cfg;
    std::vector<char> payload;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CodecMultipartDto, header, raw_cfg, payload)
};

struct MultipartPartInput
{
    std::string name;
    std::string data;
};

ContentView MakeView(const std::string &content_type, const std::string &body)
{
    return ContentView{
        .data = body.data(),
        .size = body.size(),
        .meta = ParseContentMetaFromHttpHeader(content_type),
    };
}

std::string MakeMultipartBody(const std::string &boundary, const std::vector<MultipartPartInput> &parts)
{
    std::string body;
    for(const auto &part : parts)
    {
        body.append("--").append(boundary).append("\r\n");
        body.append("Content-Disposition: form-data; name=\"").append(part.name).append("\"\r\n");
        body.append("\r\n");
        body.append(part.data);
        body.append("\r\n");
    }
    body.append("--").append(boundary).append("--\r\n");
    return body;
}

} // namespace

namespace kit_muduo {

template<>
struct MultipartObjectBinder<CodecMultipartDto>
{
    static ContentCodecResult Bind(const MultiFormParser::PartMap &parts, CodecMultipartDto *out)
    {
        auto result = ParseJsonPartToObject(parts, "header", &out->header);
        if(!result.ok)
        {
            return result;
        }

        result = ParseJsonPartToRaw(parts, "raw_cfg", out->raw_cfg);
        if(!result.ok)
        {
            return result;
        }

        return ParseOctetStreamPartToRaw(parts, "payload", out->payload);
    }
};

} // namespace kit_muduo

/*
测试思路：
1. Content-Type resolver 是后续 BindJson/BindMultipart 的第一道分流，必须识别计划中列出的主格式。
2. 覆盖 application/json 参数、+json 后缀、multipart 参数提取、XML、plain、octet-stream 和未知类型。
3. multipart 的 boundary 需要保存在 params 中，否则后续 multipart decoder 无法解析 body。

示例：
  "multipart/form-data; boundary=KITXXX; a=a"
          |
          v
  format=kMultipart, params["boundary"]="KITXXX"
*/
TEST(ContentCodecTest, ParseContentMetaFromHttpHeader)
{
    std::vector<const char*> jsons = {
        "application/json",
        "application/json; charset=utf-8",
        "application/problem+json",
    };

    for(auto &j :jsons)
    {
        auto meta = ParseContentMetaFromHttpHeader(j);
        ASSERT_EQ(meta.format, ContentFormat::kJson);
    }

    std::vector<const char*> xmls = {
        "application/xml",
        "text/xml",
        "application/*+xml",
    };

    for(auto &x :xmls)
    {
        auto meta = ParseContentMetaFromHttpHeader(x);
        ASSERT_EQ(meta.format, ContentFormat::kXml);
    }

    auto meta = ParseContentMetaFromHttpHeader("multipart/form-data; boundary=KITXXX; a=a; b=b");
    ASSERT_EQ(meta.format, ContentFormat::kMultipart);
    ASSERT_EQ(meta.media_type, "multipart/form-data");
    auto it = meta.params.find("boundary");
    ASSERT_TRUE(it != meta.params.end());
    ASSERT_EQ(it->second, "KITXXX");
    it = meta.params.find("a");
    ASSERT_TRUE(it != meta.params.end());
    ASSERT_EQ(it->second, "a");
    it = meta.params.find("b");
    ASSERT_TRUE(it != meta.params.end());
    ASSERT_EQ(it->second, "b");

    meta = ParseContentMetaFromHttpHeader("text/plain");
    ASSERT_EQ(meta.format, ContentFormat::kPlainText);

    meta = ParseContentMetaFromHttpHeader("application/octet-stream");
    ASSERT_EQ(meta.format, ContentFormat::kOctetStream);

    meta = ParseContentMetaFromHttpHeader("application/x-kit-custom");
    ASSERT_EQ(meta.format, ContentFormat::kUnknown);
}

/*
测试思路：
1. JSON decoder 要能把 application/json 请求体绑定到普通 DTO。
2. 输入是完整 JSON object，字段类型与 DTO 一致。
3. Decode 成功后 DTO 字段应被填充，证明 base codec 不依赖 HTTP handler 也能工作。

示例：
  Content-Type=application/json
  body={"id":7,"name":"kit"}
        |
        v
  CodecJsonDto{id=7,name="kit"}
*/
TEST(ContentCodecTest, JsonDecodeBindsObject)
{
    const std::string body = R"({"id":7,"name":"kit"})";
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeView("application/json", body),
        &dto,
        {ContentFormat::kJson});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.id, 7);
    EXPECT_EQ(dto.name, "kit");
}

/*
测试思路：
1. JSON body 不是完整 JSON 时，decoder 必须返回 kDecodeFailed。
2. 这类错误应该停留在 base codec 层，不进入 handler 业务校验。
3. 断言错误码，避免调用方只能拿到无原因的 bool=false。

示例：
  body={"id":
        |
        v
  kDecodeFailed
*/
TEST(ContentCodecTest, JsonDecodeRejectsBrokenJsonWithDecodeFailed)
{
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeView("application/json", R"({"id":)"),
        &dto,
        {ContentFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kDecodeFailed);
    EXPECT_NE(result.message.find("json decode failed"), std::string::npos);
}

/*
测试思路：
1. JSON 语法正确但字段类型与 DTO 不匹配，也属于绑定失败。
2. 输入 id="not-int"，nlohmann::json::get_to 应抛异常。
3. decoder 返回 kDecodeFailed，handler 可统一转成 body parse error。

示例：
  body={"id":"not-int","name":"kit"}
        |
        v
  kDecodeFailed
*/
TEST(ContentCodecTest, JsonDecodeRejectsFieldTypeMismatchWithDecodeFailed)
{
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeView("application/json", R"({"id":"not-int","name":"kit"})"),
        &dto,
        {ContentFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kDecodeFailed);
}

/*
测试思路：
1. Decode pipeline 先检查目标指针，避免后续 decoder 解引用空指针。
2. 传入 nullptr 时应返回 kInternalError。
3. 该用例固定基础防御逻辑，避免调用方传错参数导致崩溃。

示例：
  out=nullptr
      |
      v
  kInternalError
*/
TEST(ContentCodecTest, DecodeRejectsNullTarget)
{
    const std::string body = R"({"id":7,"name":"kit"})";

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeView("application/json", body),
        nullptr,
        {ContentFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kInternalError);
}

/*
测试思路：
1. BindJson 只允许 JSON 格式，base pipeline 应在格式不匹配时提前拒绝。
2. 输入 Content-Type=text/plain，即使 body 看起来像 JSON，也不能被 JSON DTO 绑定。
3. 该用例保证 HTTP Content-Type 契约是显式的。

示例：
  Content-Type=text/plain, body={"id":7}
        |
        v
  kUnsupportedFormat
*/
TEST(ContentCodecTest, DecodeRejectsFormatOutsideAllowedList)
{
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeView("text/plain", R"({"id":7,"name":"kit"})"),
        &dto,
        {ContentFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

/*
测试思路：
1. multipart decoder 依赖 Content-Type 中的 boundary 参数。
2. Content-Type 只有 multipart/form-data 但没有 boundary 时，不能尝试解析 body。
3. 返回 kInvalidContentType，方便 handler 日志定位请求头问题。

示例：
  Content-Type=multipart/form-data
        |
        v
  kInvalidContentType
*/
TEST(ContentCodecTest, MultipartDecodeRejectsMissingBoundary)
{
    CodecMultipartDto dto;

    const auto result = ContentDecodePipeline<CodecMultipartDto>::Decode(
        MakeView("multipart/form-data", ""),
        &dto,
        {ContentFormat::kMultipart});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kInvalidContentType);
}

/*
测试思路：
1. multipart DTO 绑定由 MultipartObjectBinder 特化负责。
2. 构造 header/raw_cfg/payload 三个 part，分别覆盖 JSON object、原始 JSON 和二进制字节。
3. Decode 成功后，DTO 应保留 payload 中的 NUL 字节，证明 codec 链路是二进制安全的。

示例：
  multipart:
    header={"id":9,"name":"upload"}
    raw_cfg={"method":"POST"}
    payload="a\\0b"
        |
        v
  CodecMultipartDto.payload.size()==3
*/
TEST(ContentCodecTest, MultipartDecodeBindsSpecializedDtoAndPreservesBinaryPayload)
{
    const std::string boundary = "KIT-BOUNDARY";
    const std::string payload("a\0b", 3);
    const auto body = MakeMultipartBody(boundary, {
        {"header", R"({"id":9,"name":"upload"})"},
        {"raw_cfg", R"({"method":"POST"})"},
        {"payload", payload},
    });
    CodecMultipartDto dto;

    const auto result = ContentDecodePipeline<CodecMultipartDto>::Decode(
        MakeView("multipart/form-data; boundary=" + boundary, body),
        &dto,
        {ContentFormat::kMultipart});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.header.id, 9);
    EXPECT_EQ(dto.header.name, "upload");
    EXPECT_EQ(dto.raw_cfg, nlohmann::json({{"method", "POST"}}));
    ASSERT_EQ(dto.payload.size(), payload.size());
    EXPECT_EQ(std::string(dto.payload.begin(), dto.payload.end()), payload);
}

/*
测试思路：
1. multipart binder 对必填 part 缺失要返回结构化错误。
2. CodecMultipartDto 需要 payload，但请求只提交 header/raw_cfg。
3. 返回 kMissingField 且 field="payload"，调用方可以准确记录缺失字段。

示例：
  multipart: header + raw_cfg, no payload
        |
        v
  kMissingField, field=payload
*/
TEST(ContentCodecTest, MultipartDecodeReportsMissingRequiredPart)
{
    const std::string boundary = "KIT-MISSING";
    const auto body = MakeMultipartBody(boundary, {
        {"header", R"({"id":9,"name":"upload"})"},
        {"raw_cfg", R"({"method":"POST"})"},
    });
    CodecMultipartDto dto;

    const auto result = ContentDecodePipeline<CodecMultipartDto>::Decode(
        MakeView("multipart/form-data; boundary=" + boundary, body),
        &dto,
        {ContentFormat::kMultipart});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kMissingField);
    EXPECT_EQ(result.field, "payload");
}

/*
测试思路：
1. JSON-only DTO 不应该为了 multipart 兼容去实现无意义的 from_multi_form stub。
2. 对 CodecJsonDto 走 multipart decode 时，默认 MultipartObjectBinder 应返回 kUnsupportedTarget。
3. 该用例固定 Adapter 边界：只有显式特化过的 DTO 才能接收 multipart。

示例：
  Decode<CodecJsonDto>(multipart body)
        |
        v
  kUnsupportedTarget
*/
TEST(ContentCodecTest, MultipartDecodeForJsonOnlyDtoReturnsUnsupportedTarget)
{
    const std::string boundary = "KIT-JSON-ONLY";
    const auto body = MakeMultipartBody(boundary, {
        {"header", R"({"id":9,"name":"upload"})"},
    });
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeView("multipart/form-data; boundary=" + boundary, body),
        &dto,
        {ContentFormat::kMultipart});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedTarget);
}
