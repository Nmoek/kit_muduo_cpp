/**
 * @file test_http_content_codec.cpp
 * @brief HTTP content codec 测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "gtest/gtest.h"

#include "net/http/http_content.h"
#include "net/http/http_content_codec.h"
#include "net/http/multiform.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace kit_muduo::http;

namespace http_content_codec_test {

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
    std::string content_type;
    std::string data;
};

ContentView MakeContentView(const std::string& content_type, const std::string& body)
{
    return ContentView{
        .data = reinterpret_cast<const uint8_t*>(body.data()),
        .size = body.size(),
        .meta = ParseHttpContentType(content_type),
    };
}

std::string MakeMultipartBody(const std::string& boundary,
                              const std::vector<MultipartPartInput>& parts)
{
    std::ostringstream oss;
    for(const auto& part : parts)
    {
        oss << "--" << boundary << "\r\n";
        oss << "Content-Disposition: form-data; name=\"" << part.name << "\"\r\n";
        if(!part.content_type.empty())
        {
            oss << "Content-Type: " << part.content_type << "\r\n";
        }
        oss << "\r\n";
        oss << part.data;
        oss << "\r\n";
    }
    oss << "--" << boundary << "--\r\n";
    return oss.str();
}

void from_multiform(const MultiForm& form, CodecMultipartDto& out)
{
    ThrowIfFailed(DecodeMultiPartHelper(
        form.at("header"),
        out.header,
        {ContentCodecFormat::kJson}));

    ThrowIfFailed(DecodeMultiPartHelper(
        form.at("raw_cfg"),
        out.raw_cfg,
        {ContentCodecFormat::kJson}));

    ThrowIfFailed(DecodeMultiPartToRaw(
        form.at("payload"),
        out.payload));
}

} // namespace http_content_codec_test

using http_content_codec_test::CodecJsonDto;
using http_content_codec_test::CodecMultipartDto;
using http_content_codec_test::MakeContentView;
using http_content_codec_test::MakeMultipartBody;
using http_content_codec_test::MultipartPartInput;

/*
测试思路：
1. ContentDecodePipeline<T> 在 HTTP 层按 ContentCodecFormat 分发。
2. application/json 请求体应复用 JsonCodec 绑定 DTO。
3. 成功后 DTO 字段被填充。

示例：
  Content-Type=application/json, body={"id":7,"name":"kit"} -> dto.id=7
*/
TEST(HttpContentCodecTest, JsonDecodeBindsObject)
{
    const std::string body = R"({"id":7,"name":"kit"})";
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeContentView("application/json", body),
        dto,
        {ContentCodecFormat::kJson});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.id, 7);
    EXPECT_EQ(dto.name, "kit");
}

/*
测试思路：
1. JSON 语法错误应停留在 decode failed。
2. HTTP codec 需要把 base CodecResult 转成 ContentCodecResult。
3. handler 可据此统一返回 body parse error。

示例：
  body={"id": -> kDecodeFailed
*/
TEST(HttpContentCodecTest, JsonDecodeRejectsBrokenJson)
{
    const std::string body = R"({"id":)";
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeContentView("application/json", body),
        dto,
        {ContentCodecFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kDecodeFailed);
}

/*
测试思路：
1. plain text decoder 只允许 ContentCodecFormat::kText。
2. 输出 string 应复制全部字节。
3. 该用例覆盖 ContentDecoder<std::string, kPlainText> 特化。

示例：
  text/plain + "hello" -> out == "hello"
*/
TEST(HttpContentCodecTest, PlainTextDecodeCopiesString)
{
    const std::string body = "hello";
    std::string out;

    const auto result = ContentDecodePipeline<std::string>::Decode(
        MakeContentView("text/plain", body),
        out,
        {ContentCodecFormat::kText});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(out, body);
}

/*
测试思路：
1. octet-stream decoder 直接复制原始 bytes。
2. 输入包含 NUL 字节时不能截断。
3. 该用例覆盖 HTTP 层 raw copy 的二进制安全性。

示例：
  {'a','\0','b'} -> vector<uint8_t>.size()==3
*/
TEST(HttpContentCodecTest, OctetStreamDecodePreservesBinaryBytes)
{
    const std::string body("a\0b", 3);
    std::vector<uint8_t> out;

    const auto result = ContentDecodePipeline<std::vector<uint8_t>>::Decode(
        MakeContentView("application/octet-stream", body),
        out,
        {ContentCodecFormat::kBinary});

    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(out.size(), body.size());
    EXPECT_EQ(std::memcmp(out.data(), body.data(), body.size()), 0);
}

/*
测试思路：
1. allowed_formats 是 handler 契约，必须先于具体 decoder 检查。
2. body 看起来像 JSON，但 Content-Type=text/plain 时不能绑定 JSON DTO。
3. 返回 kUnsupportedFormat。

示例：
  text/plain + {"id":7} + allowed={kJson} -> kUnsupportedFormat
*/
TEST(HttpContentCodecTest, DecodeRejectsFormatOutsideAllowedList)
{
    const std::string body = R"({"id":7,"name":"kit"})";
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeContentView("text/plain", body),
        dto,
        {ContentCodecFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

/*
测试思路：
1. application/x-www-form-urlencoded 本轮只识别为 ContentCodecFormat::kFormUrlEncoded。
2. 即使 handler 明确允许该 codec 格式，也不能落到 DTO 泛型 decoder 的 unsupported target。
3. 返回信息应明确为 form-url-encoded decoder not enabled，避免业务误以为普通 HTML form 已支持自动绑定。

示例：
  Content-Type=application/x-www-form-urlencoded + allowed={kFormUrlEncoded}
    -> kUnsupportedFormat + decoder not enabled
*/
TEST(HttpContentCodecTest, FormUrlEncodedDecodeReportsDecoderNotEnabled)
{
    const std::string body = "id=7&name=kit";
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeContentView("application/x-www-form-urlencoded", body),
        dto,
        {ContentCodecFormat::kFormUrlEncoded});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
    EXPECT_NE(result.message.find("form-url-encoded decoder not enabled"), std::string::npos);
}

/*
测试思路：
1. multipart/form-data 整包 decoder 必须依赖 boundary 参数。
2. 缺 boundary 时不应进入 MultiForm 原始字节扫描。
3. 返回 kInvalidContentType，便于定位请求头错误。

示例：
  Content-Type=multipart/form-data -> multipart boundary missing
*/
TEST(HttpContentCodecTest, MultipartDecodeRejectsMissingBoundary)
{
    const std::string body;
    CodecMultipartDto dto;

    const auto result = ContentDecodePipeline<CodecMultipartDto>::Decode(
        MakeContentView("multipart/form-data", body),
        dto,
        {ContentCodecFormat::kMultipartFormData});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kInvalidContentType);
    EXPECT_NE(result.message.find("boundary"), std::string::npos);
}

/*
测试思路：
1. multipart DTO 绑定通过 DTO namespace 内的 ADL from_multiform 实现。
2. JSON part 必须声明 application/json，raw payload 通过 DecodeMultiPartToRaw 原样提取。
3. payload 中包含 NUL 字节时不能丢失。

示例：
  header/json + raw_cfg/json + payload="a\0b" -> DTO 全部填充
*/
TEST(HttpContentCodecTest, MultipartDecodeBindsAdlDtoAndPreservesBinaryPayload)
{
    const std::string boundary = "KIT-BOUNDARY";
    const std::string payload("a\0b", 3);
    const auto body = MakeMultipartBody(boundary, {
        {"header", "application/json", R"({"id":9,"name":"upload"})"},
        {"raw_cfg", "application/json", R"({"method":"POST"})"},
        {"payload", "application/octet-stream", payload},
    });
    CodecMultipartDto dto;

    const auto result = ContentDecodePipeline<CodecMultipartDto>::Decode(
        MakeContentView("multipart/form-data; boundary=" + boundary, body),
        dto,
        {ContentCodecFormat::kMultipartFormData});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.header.id, 9);
    EXPECT_EQ(dto.header.name, "upload");
    EXPECT_EQ(dto.raw_cfg, nlohmann::json({{"method", "POST"}}));
    ASSERT_EQ(dto.payload.size(), payload.size());
    EXPECT_EQ(std::string(dto.payload.begin(), dto.payload.end()), payload);
}

/*
测试思路：
1. DTO 的 from_multiform 使用 form.at(name) 访问单值必填字段。
2. 缺 payload 时 form.at("payload") 抛 MultiFormException。
3. HTTP decoder 边界应把异常转成 kDecodeFailed，并保留缺字段信息。

示例：
  multipart: header + raw_cfg, no payload -> message contains missing multipart field
*/
TEST(HttpContentCodecTest, MultipartDecodeReportsMissingRequiredPart)
{
    const std::string boundary = "KIT-MISSING";
    const auto body = MakeMultipartBody(boundary, {
        {"header", "application/json", R"({"id":9,"name":"upload"})"},
        {"raw_cfg", "application/json", R"({"method":"POST"})"},
    });
    CodecMultipartDto dto;

    const auto result = ContentDecodePipeline<CodecMultipartDto>::Decode(
        MakeContentView("multipart/form-data; boundary=" + boundary, body),
        dto,
        {ContentCodecFormat::kMultipartFormData});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kDecodeFailed);
    EXPECT_NE(result.message.find("missing multipart field: payload"), std::string::npos);
}

/*
测试思路：
1. 只有 JSON from_json、没有 ADL from_multiform 的 DTO 不应该默默接受 multipart。
2. MultiForm::get_to 会抛 multipart target unsupported。
3. HTTP decoder 返回 kDecodeFailed 并保留 unsupported 信息。

示例：
  CodecJsonDto + multipart body -> multipart target unsupported
*/
TEST(HttpContentCodecTest, MultipartDecodeForUnsupportedDtoReportsUnsupportedTarget)
{
    const std::string boundary = "KIT-UNSUPPORTED";
    const auto body = MakeMultipartBody(boundary, {
        {"header", "application/json", R"({"id":9,"name":"upload"})"},
    });
    CodecJsonDto dto;

    const auto result = ContentDecodePipeline<CodecJsonDto>::Decode(
        MakeContentView("multipart/form-data; boundary=" + boundary, body),
        dto,
        {ContentCodecFormat::kMultipartFormData});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kDecodeFailed);
    EXPECT_NE(result.message.find("multipart target unsupported"), std::string::npos);
}

/*
测试思路：
1. DecodeMultiPartHelper 是 part 级“解释内容”入口，必须检查 part Content-Type。
2. body 虽然是合法 JSON，但 part Content-Type=text/plain 时不能当 JSON 解。
3. 返回 kUnsupportedFormat。

示例：
  part(text/plain, "{}") + allowed={kJson} -> kUnsupportedFormat
*/
TEST(HttpContentCodecTest, DecodeMultiPartHelperRejectsPartFormatMismatch)
{
    const std::string boundary = "KIT-PART-FORMAT";
    const auto body = MakeMultipartBody(boundary, {
        {"header", "text/plain", R"({"id":9,"name":"upload"})"},
    });
    const auto form = MultiForm::parse(
        reinterpret_cast<const uint8_t*>(body.data()),
        body.size(),
        boundary);
    CodecJsonDto dto;

    const auto result = DecodeMultiPartHelper(
        form.at("header"),
        dto,
        {ContentCodecFormat::kJson});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}
