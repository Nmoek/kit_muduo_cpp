/**
 * @file test_http_context.cpp
 * @brief HttpContext body bind facade 测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "gtest/gtest.h"

#include "net/http/http_context.h"
#include "net/http/http_content_codec.h"
#include "net/http/http_request.h"
#include "net/call_backs.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace kit_muduo::http;

namespace {

struct HttpBindJsonDto
{
    int32_t id{0};
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HttpBindJsonDto, id, name)
};

struct HttpBindMultipartDto
{
    HttpBindJsonDto header;
    std::vector<char> body;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HttpBindMultipartDto, header, body)
};

std::string MakeMultipartBody(const std::string &boundary,
                              const std::string &header_json,
                              const std::string &body_data)
{
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"header\"\r\n";
    oss << "Content-Type: application/json\r\n";
    oss << "\r\n";
    oss << header_json << "\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"body\"\r\n";
    oss << "Content-Type: application/octet-stream\r\n";
    oss << "\r\n";
    oss << body_data << "\r\n";
    oss << "--" << boundary << "--\r\n";
    return oss.str();
}

kit_muduo::HttpContextPtr MakeBindContext(const std::string &content_type, const std::string &body)
{
    auto ctx = std::make_shared<HttpContext>();
    auto req = ctx->request();
    req->setVersion(Version::kHttp11);
    req->setMethod(HttpRequest::Method::kPost);
    req->setPath("/bind-test");
    req->addHeader("Content-Type", content_type);
    Body req_body;
    req_body.appendData(body);
    req->setBody(req_body);
    return ctx;
}

void from_multiform(const MultiForm& form, HttpBindMultipartDto& out)
{
    ThrowIfFailed(DecodeMultiPartHelper(
        form.at("header"),
        out.header,
        {ContentFormat::kJson}));

    ThrowIfFailed(DecodeMultiPartToRaw(
        form.at("body"),
        out.body));
}



} // namespace

/*
测试思路：
1. HttpContext::bindJson 是 Web 层 facade，只允许 application/json 请求体。
2. 请求头带 charset 参数时，base Content-Type resolver 仍应识别为 JSON。
3. 绑定成功后 DTO 字段被填充，证明 facade 正确把 HttpRequest 包装成 ContentView。

示例：
  Content-Type=application/json; charset=utf-8
  body={"id":11,"name":"ctx-json"}
        |
        v
  bindJson ok
*/
TEST(TestHttpContextBind, BindJsonAcceptsJsonWithCharset)
{
    auto ctx = MakeBindContext(
        "application/json; charset=utf-8",
        R"({"id":11,"name":"ctx-json"})");
    HttpBindJsonDto dto;

    const auto result = ctx->bindJson(dto);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.id, 11);
    EXPECT_EQ(dto.name, "ctx-json");
}

/*
测试思路：
1. bindJson 的接口契约是 JSON-only。
2. 请求 Content-Type 为 text/plain，即使 body 内容是合法 JSON，也必须拒绝。
3. 返回 kUnsupportedFormat，handler 可以统一转成 body parse error。

示例：
  Content-Type=text/plain, body={"id":11}
        |
        v
  bindJson failed
*/
TEST(TestHttpContextBind, BindJsonRejectsPlainText)
{
    auto ctx = MakeBindContext(
        "text/plain",
        R"({"id":11,"name":"ctx-json"})");
    HttpBindJsonDto dto;

    const auto result = ctx->bindJson(dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

/*
测试思路：
1. bindJson 不能接受 multipart/form-data，否则 JSON-only handler 的契约会变得模糊。
2. 构造一个合法 multipart 请求体，但调用 bindJson。
3. facade 应在 allowed_formats 检查阶段返回 kUnsupportedFormat。

示例：
  multipart/form-data + bindJson
        |
        v
  kUnsupportedFormat
*/
TEST(TestHttpContextBind, BindJsonRejectsMultipart)
{
    const std::string boundary = "CTX-MULTIPART-JSON-REJECT";
    auto ctx = MakeBindContext(
        "multipart/form-data; boundary=" + boundary,
        MakeMultipartBody(boundary, R"({"id":12,"name":"ctx-multipart"})", "abc"));
    HttpBindJsonDto dto;

    const auto result = ctx->bindJson(dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

/*
测试思路：
1. bindMultipart 是 multipart-only facade。
2. 构造 header JSON part 和 body 原始字节 part，DTO 有对应 ADL from_multiform。
3. 绑定成功后，header 对象和 body 字节都应正确写入 DTO。

示例：
  multipart:
    header={"id":13,"name":"ctx-multipart"}
    body="payload"
        |
        v
  bindMultipart ok
*/
TEST(TestHttpContextBind, BindMultipartAcceptsMultipartWithBoundary)
{
    const std::string boundary = "CTX-MULTIPART-OK";
    auto ctx = MakeBindContext(
        "multipart/form-data; boundary=" + boundary,
        MakeMultipartBody(boundary, R"({"id":13,"name":"ctx-multipart"})", "payload"));
    HttpBindMultipartDto dto;

    const auto result = ctx->bindMultipart(dto);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.header.id, 13);
    EXPECT_EQ(dto.header.name, "ctx-multipart");
    EXPECT_EQ(std::string(dto.body.begin(), dto.body.end()), "payload");
}

/*
测试思路：
1. bindMultipart 不能接受 JSON 请求体。
2. 该行为保证 AddProtocol/DetailBody 这类 multipart-only handler 不会误收 JSON body。
3. facade 应直接返回 kUnsupportedFormat，不进入 from_multiform。

示例：
  Content-Type=application/json + bindMultipart
        |
        v
  kUnsupportedFormat
*/
TEST(TestHttpContextBind, BindMultipartRejectsJson)
{
    auto ctx = MakeBindContext(
        "application/json",
        R"({"id":13,"name":"ctx-json"})");
    HttpBindMultipartDto dto;

    const auto result = ctx->bindMultipart(dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}
