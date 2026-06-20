/**
 * @file test_http_content.cpp
 * @brief HTTP content meta 解析测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "gtest/gtest.h"

#include "net/http/http_content.h"
#include "net/http/http_util.h"

#include <string>

using namespace kit_muduo::http;

/*
测试思路：
1. HTTP Content-Type resolver 只解析 HTTP/MIME 语义，不下沉到 base。
2. 覆盖 JSON、application/...+json、multipart、XML、text、octet-stream 和未知类型。
3. image/svg+xml 不属于 05 文档第一版 HTTP bind 格式，不应被当成 XML DTO bind。

示例：
  application/problem+json -> kJson
  image/svg+xml            -> kUnknown
*/
TEST(HttpContentTest, ParseHttpContentTypeRecognizesConfiguredFormats)
{
    EXPECT_EQ(ParseHttpContentType("application/json").format, ContentFormat::kJson);
    EXPECT_EQ(ParseHttpContentType("application/problem+json").format, ContentFormat::kJson);
    EXPECT_EQ(ParseHttpContentType("application/not-json").format, ContentFormat::kUnknown);

    EXPECT_EQ(ParseHttpContentType("multipart/form-data; boundary=KIT").format,
              ContentFormat::kMultipartFormData);

    EXPECT_EQ(ParseHttpContentType("application/xml").format, ContentFormat::kXml);
    EXPECT_EQ(ParseHttpContentType("text/xml").format, ContentFormat::kXml);
    EXPECT_EQ(ParseHttpContentType("application/soap+xml").format, ContentFormat::kXml);
    EXPECT_EQ(ParseHttpContentType("image/svg+xml").format, ContentFormat::kUnknown);

    EXPECT_EQ(ParseHttpContentType("text/plain").format, ContentFormat::kPlainText);
    EXPECT_EQ(ParseHttpContentType("application/octet-stream").format, ContentFormat::kOctetStream);
    EXPECT_EQ(ParseHttpContentType("application/x-kit-custom").format, ContentFormat::kUnknown);
}

/*
测试思路：
1. HTTP Content-Type 参数名应统一小写，值应去除首尾空白和包裹引号。
2. multipart boundary 通过 params 传给 MultiForm parser。
3. 同时验证 media type 大小写被规范化。

示例：
  Multipart/Form-Data; Boundary="AaB03x"; Charset=utf-8
    -> media_type=multipart/form-data, params["boundary"]=AaB03x
*/
TEST(HttpContentTest, ParseHttpContentTypeExtractsParams)
{
    const auto meta = ParseHttpContentType(
        "Multipart/Form-Data; Boundary=\"AaB03x\"; Charset=utf-8");

    EXPECT_EQ(meta.format, ContentFormat::kMultipartFormData);
    EXPECT_EQ(meta.media_type, "multipart/form-data");
    ASSERT_TRUE(meta.params.count("boundary"));
    EXPECT_EQ(meta.params.at("boundary"), "AaB03x");
    ASSERT_TRUE(meta.params.count("charset"));
    EXPECT_EQ(meta.params.at("charset"), "utf-8");
}

/*
测试思路：
1. HTTP 整包缺 Content-Type 时，handler 不应猜测格式。
2. multipart part 缺 Content-Type 时，按表单默认语义视为 text/plain。
3. 第一版不保存额外布尔字段，缺省通过 raw_content_type.empty() 判断。

示例：
  ParseHttpContentType("")          -> kUnknown
  ParseMultiformPartContentType("") -> kPlainText
*/
TEST(HttpContentTest, MissingContentTypeDefaultsDifferByContext)
{
    const auto http_meta = ParseHttpContentType("");
    EXPECT_TRUE(http_meta.raw_content_type.empty());
    EXPECT_EQ(http_meta.format, ContentFormat::kUnknown);
    EXPECT_TRUE(http_meta.media_type.empty());

    const auto part_meta = ParseMultiformPartContentType("");
    EXPECT_TRUE(part_meta.raw_content_type.empty());
    EXPECT_EQ(part_meta.format, ContentFormat::kPlainText);
    EXPECT_EQ(part_meta.media_type, "text/plain");
}

/*
测试思路：
1. legacy ContentType::FromString 仍被 HTTP parser 写入 Body::contentType。
2. 它必须和新的 resolver 一样先剥离参数、再按 media type 精确识别。
3. 非标准但包含 json/xml 字样的类型不能被子串误判。

示例：
  application/not-json -> kUnknowType
  application/problem+json; charset=utf-8 -> kJsonType
*/
TEST(HttpContentTest, LegacyContentTypeFromStringUsesPreciseMediaType)
{
    EXPECT_EQ(ContentType::FromString("application/problem+json; charset=utf-8").toInt(),
              ContentType::kJsonType);
    EXPECT_EQ(ContentType::FromString("application/not-json").toInt(),
              ContentType::kUnknowType);
    EXPECT_EQ(ContentType::FromString("image/svg+xml").toInt(),
              ContentType::kSvgXml);
    EXPECT_EQ(ContentType::FromString("text/xml").toInt(),
              ContentType::kXmlType);
    EXPECT_EQ(ContentType::FromString("multipart/form-data; boundary=KIT").toInt(),
              ContentType::kMultiForm);
    EXPECT_EQ(ContentType::FromString("application/not-stream").toInt(),
              ContentType::kUnknowType);
}


