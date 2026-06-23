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

#include <string>

using namespace kit_muduo::http;

/*
测试思路：
1. HTTP Content-Type resolver 只解析 HTTP/MIME 事实，codec 分类通过 ResolveContentCodecFormat 推导。
2. 覆盖 JSON、application/...+json、multipart、XML、text、octet-stream 和未知类型。
3. image/svg+xml 是已知资源 MIME，但 codec 分类是文本，不应被当成 XML DTO bind。

示例：
  application/problem+json -> known=kApplicationProblemJson, codec=kJson
  image/svg+xml            -> known=kImageSvgXml, codec=kText
*/
TEST(HttpContentTest, ParseHttpContentTypeRecognizesConfiguredFormats)
{
    auto meta = ParseHttpContentType("application/json");
    EXPECT_EQ(meta.known_type, KnownMediaType::kApplicationJson);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kJson);

    meta = ParseHttpContentType("application/problem+json");
    EXPECT_EQ(meta.known_type, KnownMediaType::kApplicationProblemJson);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kJson);

    meta = ParseHttpContentType("application/vnd.kit+json");
    EXPECT_EQ(meta.known_type, KnownMediaType::kCustom);
    EXPECT_EQ(meta.parsed_media_type.suffix, "json");
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kJson);

    meta = ParseHttpContentType("multipart/form-data; boundary=KIT");
    EXPECT_EQ(meta.known_type, KnownMediaType::kMultipartFormData);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kMultipartFormData);

    EXPECT_EQ(ParseHttpContentType("application/xml").known_type, KnownMediaType::kApplicationXml);
    EXPECT_EQ(ParseHttpContentType("text/xml").known_type, KnownMediaType::kTextXml);
    EXPECT_EQ(ParseHttpContentType("application/soap+xml").known_type, KnownMediaType::kApplicationSoapXml);

    meta = ParseHttpContentType("image/svg+xml");
    EXPECT_EQ(meta.known_type, KnownMediaType::kImageSvgXml);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kText);

    meta = ParseHttpContentType("text/plain");
    EXPECT_EQ(meta.known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kText);

    meta = ParseHttpContentType("application/octet-stream");
    EXPECT_EQ(meta.known_type, KnownMediaType::kApplicationOctetStream);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kBinary);

    meta = ParseHttpContentType("application/x-kit-custom");
    EXPECT_EQ(meta.known_type, KnownMediaType::kCustom);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kNone);
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

    EXPECT_EQ(meta.known_type, KnownMediaType::kMultipartFormData);
    EXPECT_EQ(ResolveContentCodecFormat(meta), ContentCodecFormat::kMultipartFormData);
    EXPECT_EQ(meta.media_type, "multipart/form-data");
    ASSERT_TRUE(meta.params.count("boundary"));
    EXPECT_EQ(meta.params.at("boundary"), "AaB03x");
    ASSERT_TRUE(meta.params.count("charset"));
    EXPECT_EQ(meta.params.at("charset"), "utf-8");
}

/*
测试思路：
1. Content-Type 参数用 quoted-string 包裹时，值内部可以包含分号，不能被当成下一个参数分隔符。
2. quoted-string 内的反斜杠转义需要还原，保证和 ToContentTypeHeaderValue 的输出规则对称。
3. 该用例覆盖常见 boundary/title 等参数包含特殊字符的场景。

示例：
  text/plain; title="a;b\"c\\d"; charset=utf-8
    -> params["title"] = a;b"c\d
*/
TEST(HttpContentTest, ParseHttpContentTypeKeepsSemicolonInsideQuotedParam)
{
    const auto meta = ParseHttpContentType(
        R"(text/plain; title="a;b\"c\\d"; charset=utf-8)");

    EXPECT_EQ(meta.known_type, KnownMediaType::kTextPlain);
    ASSERT_TRUE(meta.params.count("title"));
    EXPECT_EQ(meta.params.at("title"), "a;b\"c\\d");
    ASSERT_TRUE(meta.params.count("charset"));
    EXPECT_EQ(meta.params.at("charset"), "utf-8");
}

/*
测试思路：
1. HTTP 整包缺 Content-Type 时，handler 不应猜测格式。
2. multipart part 缺 Content-Type 时，按表单默认语义视为 text/plain。
3. 第一版不保存额外布尔字段，缺省通过 raw_content_type.empty() 判断。

示例：
  ParseHttpContentType("")          -> KnownMediaType::kUnknown
  ParseMultiformPartContentType("") -> KnownMediaType::kTextPlain
*/
TEST(HttpContentTest, MissingContentTypeDefaultsDifferByContext)
{
    const auto http_meta = ParseHttpContentType("");
    EXPECT_TRUE(http_meta.raw_content_type.empty());
    EXPECT_EQ(http_meta.known_type, KnownMediaType::kUnknown);
    EXPECT_EQ(ResolveContentCodecFormat(http_meta), ContentCodecFormat::kNone);
    EXPECT_TRUE(http_meta.media_type.empty());

    const auto part_meta = ParseMultiformPartContentType("");
    EXPECT_TRUE(part_meta.raw_content_type.empty());
    EXPECT_EQ(part_meta.known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ResolveContentCodecFormat(part_meta), ContentCodecFormat::kText);
    EXPECT_EQ(part_meta.media_type, "text/plain");
}

/*
测试思路：
1. ToContentTypeHeaderValue 按稳定顺序输出参数。
2. 参数 value 含空格时需要 quote。
3. 该能力用于 HttpRequest/HttpResponse 序列化 Content-Type。

示例：
  multipart/form-data + boundary="----Kit Boundary" -> quoted boundary
*/
TEST(HttpContentTest, ToContentTypeHeaderValueSerializesParams)
{
    auto meta = MakeContentMeta(KnownMediaType::kMultipartFormData);
    SetContentTypeParam(meta, "boundary", "----Kit Boundary");
    SetContentTypeParam(meta, "charset", "utf-8");

    EXPECT_EQ(ToContentTypeHeaderValue(meta),
              R"(multipart/form-data; boundary="----Kit Boundary"; charset=utf-8)");
}

/*
测试思路：
1. ToContentTypeHeaderValue 输出参数时需要按 key 排序，保证序列化结果稳定。
2. 参数 value 包含分号、双引号或反斜杠时必须加引号，并对双引号/反斜杠做最小转义。
3. 该用例和 ParseHttpContentTypeKeepsSemicolonInsideQuotedParam 形成参数解析/输出的往返覆盖。

示例：
  title = a;b"c\d -> title="a;b\"c\\d"
*/
TEST(HttpContentTest, ToContentTypeHeaderValueEscapesQuotedParam)
{
    auto meta = MakeContentMeta(KnownMediaType::kTextPlain);
    SetContentTypeParam(meta, "title", "a;b\"c\\d");
    SetContentTypeParam(meta, "charset", "utf-8");

    EXPECT_EQ(ToContentTypeHeaderValue(meta),
              R"CT(text/plain; charset=utf-8; title="a;b\"c\\d")CT");
}

/*
测试思路：
1. 静态资源 MIME 推断应优先使用运行期注册表，其次内置表，最后回退 octet-stream。
2. RegisterMimeTypeForExtension 接受带点/不带点的扩展名，但不接受路径。
3. GuessMediaTypeFromExtension 支持路径、文件名和纯扩展名，并忽略 query/fragment。

示例：
  /static/app.css?v=1 -> text/css
  "kit-custom-ext" 注册为 application/x-kit-custom -> a.KIT-CUSTOM-EXT 命中注册表
  /tmp/README -> application/octet-stream
*/
TEST(HttpContentTest, GuessMediaTypeFromExtensionUsesBuiltinRuntimeAndFallback)
{
    EXPECT_EQ(BuiltinMimeTypesByExtension().at(".css"), "text/css");
    EXPECT_EQ(GuessMediaTypeFromExtension("/static/app.css?v=1"), "text/css");
    EXPECT_EQ(GuessMediaTypeFromExtension(".json"), "application/json");
    EXPECT_EQ(GuessMediaTypeFromExtension("json"), "application/json");
    EXPECT_EQ(GuessMediaTypeFromExtension("/tmp/README"), "application/octet-stream");

    ASSERT_TRUE(RegisterMimeTypeForExtension("kit-custom-ext", "Application/X-Kit-Custom"));
    EXPECT_EQ(GuessMediaTypeFromExtension("/assets/a.KIT-CUSTOM-EXT#v"), "application/x-kit-custom");

    EXPECT_FALSE(RegisterMimeTypeForExtension("/tmp/a.csv", "text/csv"));
    EXPECT_FALSE(RegisterMimeTypeForExtension("bad", "not-a-media-type"));
}
