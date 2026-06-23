/**
 * @file test_multiform.cpp
 * @brief MultiForm multipart/form-data 解析测试
 * @author Kewin Li
 * @date 2026-05-06
 */
#include "gtest/gtest.h"

#include "net/http/multiform.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace kit_muduo::http;

namespace {

std::string MakeSimpleBody(const std::string& boundary,
                           const std::string& name,
                           const std::string& value)
{
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"" << name << "\"\r\n";
    oss << "\r\n";
    oss << value << "\r\n";
    oss << "--" << boundary << "--\r\n";
    return oss.str();
}

MultiForm ParseStringBody(const std::string& body, const std::string& boundary)
{
    return MultiForm::parse(
        reinterpret_cast<const uint8_t*>(body.data()),
        body.size(),
        boundary);
}

std::string PartText(const FormPart& part)
{
    return std::string(part.data.begin(), part.data.end());
}

} // namespace

/*
测试思路：
1. MultiForm::parse 现在直接接收 boundary，不再解析完整 Content-Type。
2. boundary 为空属于调用方传入的 HTTP Content-Type 参数错误。
3. 该用例固定异常模型：值对象接口直接抛 MultiFormException。

示例：
  boundary="" -> throw "multipart boundary missing"
*/
TEST(MultiFormTest, EmptyBoundaryThrows)
{
    const std::string body = "--abc\r\n";

    EXPECT_THROW(
        MultiForm::parse(reinterpret_cast<const uint8_t*>(body.data()), body.size(), ""),
        MultiFormException);
}

/*
测试思路：
1. multipart body 没有任何字节时不能进入指针扫描。
2. parser 应以异常失败，而不是返回半初始化对象或触发 undefined behavior。
3. 该用例覆盖空 body 防御逻辑。

示例：
  data=null, len=0 -> throw
*/
TEST(MultiFormTest, EmptyBodyThrows)
{
    EXPECT_THROW(MultiForm::parse(nullptr, 0, "abc"), MultiFormException);
}

/*
测试思路：
1. 标准 HTML 表单提交单个文本字段。
2. MultiForm 应建立字段索引，contains/count/at 都可用。
3. part 未声明 Content-Type 时，默认作为 text/plain。

示例：
  username=john -> form.at("username").data == "john"
*/
TEST(MultiFormTest, SingleTextField)
{
    const std::string boundary = "boundary123";
    const auto body = MakeSimpleBody(boundary, "username", "john");

    const auto form = ParseStringBody(body, boundary);

    ASSERT_FALSE(form.empty());
    EXPECT_TRUE(form.contains("username"));
    EXPECT_EQ(form.count("username"), 1u);
    const auto& part = form.at("username");
    EXPECT_EQ(part.name, "username");
    EXPECT_TRUE(part.filename.empty());
    EXPECT_FALSE(part.isFile());
    EXPECT_EQ(PartText(part), "john");
    EXPECT_EQ(part.meta.known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ResolveContentCodecFormat(part.meta), ContentCodecFormat::kText);
    EXPECT_EQ(part.meta.media_type, "text/plain");
}

/*
测试思路：
1. 一个 multipart body 包含多个普通字段。
2. 每个字段应独立索引，不互相覆盖。
3. 该用例覆盖基础字段扫描循环。

示例：
  a=1,b=2,c=3 -> count(a/b/c)==1
*/
TEST(MultiFormTest, MultipleTextFields)
{
    const std::string boundary = "b0";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"a\"\r\n\r\n";
    oss << "1\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"b\"\r\n\r\n";
    oss << "2\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"c\"\r\n\r\n";
    oss << "3\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    EXPECT_EQ(form.count("a"), 1u);
    EXPECT_EQ(form.count("b"), 1u);
    EXPECT_EQ(form.count("c"), 1u);
    EXPECT_EQ(PartText(form.at("a")), "1");
    EXPECT_EQ(PartText(form.at("b")), "2");
    EXPECT_EQ(PartText(form.at("c")), "3");
}

/*
测试思路：
1. 新 MultiForm 语义是保留同名字段，而不是后者覆盖前者。
2. all(name) 应返回两个 part，保持提交顺序。
3. at(name) 代表单值访问，遇到重复字段必须抛异常。

示例：
  dup=first, dup=second -> count(dup)==2, at(dup) throws
*/
TEST(MultiFormTest, DuplicateFieldsArePreservedAndAtThrows)
{
    const std::string boundary = "dup-boundary";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"dup\"\r\n\r\n";
    oss << "first\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"dup\"\r\n\r\n";
    oss << "second\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    EXPECT_EQ(form.count("dup"), 2u);
    const auto& parts = form.all("dup");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(PartText(parts[0]), "first");
    EXPECT_EQ(PartText(parts[1]), "second");
    EXPECT_THROW(form.at("dup"), MultiFormException);
}

/*
测试思路：
1. 文件 part 的 Content-Disposition 会携带 filename。
2. Content-Type 应解析进 part.meta，自定义 headers 之外的格式解释不在 MultiForm 中做。
3. 原始数据需要完整保留。

示例：
  name=avatar, filename=photo.png, Content-Type=image/png
*/
TEST(MultiFormTest, FileUploadPreservesFilenameContentTypeAndBytes)
{
    const std::string boundary = "fileboundary";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"avatar\"; filename=\"photo.png\"\r\n";
    oss << "Content-Type: image/png\r\n";
    oss << "\r\n";
    oss << "\x89PNG\r\nfake_data";
    oss << "\r\n--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("avatar"), 1u);
    const auto& part = form.at("avatar");
    EXPECT_EQ(part.name, "avatar");
    EXPECT_EQ(part.filename, "photo.png");
    EXPECT_TRUE(part.isFile());
    EXPECT_EQ(part.meta.raw_content_type, " image/png");
    EXPECT_EQ(part.meta.media_type, "image/png");
    EXPECT_EQ(part.meta.known_type, KnownMediaType::kImagePng);
    EXPECT_EQ(ResolveContentCodecFormat(part.meta), ContentCodecFormat::kBinary);
    EXPECT_EQ(PartText(part), std::string("\x89PNG\r\nfake_data"));
}

/*
测试思路：
1. body 内嵌 NUL 字节时，multipart parser 必须按长度处理。
2. 解析后的 FormPart::data 应和输入字节完全一致。
3. 该用例防止回退到 strlen/C 字符串路径。

示例：
  {'H','e','\0','l','l','\0','o'} -> size==7
*/
TEST(MultiFormTest, BinaryBodyWithNullBytes)
{
    const std::string boundary = "binbound";
    const char binary_data[] = {'H', 'e', '\0', 'l', 'l', '\0', 'o'};

    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"bin\"\r\n";
    oss << "\r\n";
    oss.write(binary_data, sizeof(binary_data));
    oss << "\r\n--" << boundary << "--\r\n";

    const std::string body = oss.str();
    const auto form = ParseStringBody(body, boundary);

    const auto& part = form.at("bin");
    ASSERT_EQ(part.data.size(), sizeof(binary_data));
    EXPECT_EQ(std::memcmp(part.data.data(), binary_data, sizeof(binary_data)), 0);
}

/*
测试思路：
1. part body 中可能包含类似 boundary 的字节片段。
2. 只有后缀字符合法的 marker 才能作为真实 boundary。
3. "\r\n--myboundaryXY" 中 XY 不是合法后缀，应保留在 body 中。

示例：
  body contains "\r\n--myboundaryXY" -> not split
*/
TEST(MultiFormTest, BodyContainsBoundaryLikePattern)
{
    const std::string boundary = "myboundary";
    const char pattern[] = "\r\n--myboundaryXY";

    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"safe\"\r\n";
    oss << "\r\n";
    oss.write(pattern, sizeof(pattern));
    oss << "\r\n--" << boundary << "--\r\n";

    const std::string body = oss.str();
    const auto form = ParseStringBody(body, boundary);

    const auto& part = form.at("safe");
    ASSERT_EQ(part.data.size(), sizeof(pattern));
    EXPECT_EQ(std::memcmp(part.data.data(), pattern, sizeof(pattern)), 0);
}

/*
测试思路：
1. 空字段值是合法 multipart 内容。
2. part 应存在，但 data 为空。
3. 该用例区别“字段缺失”和“字段值为空”。

示例：
  name=empty, body="" -> contains(empty), data.empty()
*/
TEST(MultiFormTest, EmptyFieldValue)
{
    const std::string boundary = "emptyb";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"empty\"\r\n";
    oss << "\r\n";
    oss << "\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("empty"), 1u);
    EXPECT_TRUE(form.at("empty").data.empty());
}

/*
测试思路：
1. 第一个 boundary 前可以有 preamble。
2. parser 应忽略 preamble，从第一个 boundary 开始解析。
3. 该用例覆盖 std::search 查找首 boundary 的路径。

示例：
  "preamble\r\n--pb..." -> field=value
*/
TEST(MultiFormTest, PreambleIgnored)
{
    const std::string boundary = "pb";
    std::ostringstream oss;
    oss << "This is preamble text that should be ignored\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"field\"\r\n";
    oss << "\r\n";
    oss << "value\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("field"), 1u);
    EXPECT_EQ(PartText(form.at("field")), "value");
}

/*
测试思路：
1. 关闭 boundary 后可以有 epilogue。
2. parser 遇到 final boundary 后停止，不把 epilogue 当 part。
3. 该用例覆盖 final boundary 路径。

示例：
  --epb--\r\nepilogue -> only field f
*/
TEST(MultiFormTest, EpilogueIgnored)
{
    const std::string boundary = "epb";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"f\"\r\n";
    oss << "\r\n";
    oss << "v\r\n";
    oss << "--" << boundary << "--\r\n";
    oss << "This is epilogue text to be ignored\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("f"), 1u);
    EXPECT_EQ(PartText(form.at("f")), "v");
}

/*
测试思路：
1. body 可以只有关闭 boundary，没有任何 part。
2. parser 应返回空 MultiForm。
3. 该用例覆盖 is_final 在首 boundary 后立即成立的路径。

示例：
  --onlyclose-- -> empty form
*/
TEST(MultiFormTest, OnlyFinalBoundaryNoParts)
{
    const std::string boundary = "onlyclose";
    std::ostringstream oss;
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    EXPECT_TRUE(form.empty());
}

/*
测试思路：
1. body 完全不包含 boundary 是非法 multipart。
2. MultiForm 值对象接口应抛异常。
3. 该用例替代旧 parser 返回空 PartMap 的语义。

示例：
  "just text", boundary=b0 -> throw boundary not found
*/
TEST(MultiFormTest, MissingBoundaryMarkerThrows)
{
    const std::string body = "just some text, no boundary here";

    EXPECT_THROW(ParseStringBody(body, "b0"), MultiFormException);
}

/*
测试思路：
1. 实际客户端可能只使用 LF 行尾。
2. parser 搜索 "\n--boundary"，应兼容 LF-only body。
3. 字段内容末尾的 LF 应被剥离为正文分隔符。

示例：
  --lf\n...\n--lf--\n -> f=value
*/
TEST(MultiFormTest, LfOnlyLineEndings)
{
    const std::string boundary = "lfboundary";
    std::ostringstream oss;
    oss << "--" << boundary << "\n";
    oss << "Content-Disposition: form-data; name=\"f\"\n";
    oss << "\n";
    oss << "value\n";
    oss << "--" << boundary << "--\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("f"), 1u);
    EXPECT_EQ(PartText(form.at("f")), "value");
}

/*
测试思路：
1. part 中可包含 Content-Disposition/Content-Type 以外的自定义 header。
2. 自定义 header 应存入 FormPart::headers。
3. Content-Type 不应重复放进 headers，因为它有专门的 meta。

示例：
  X-Custom: hello -> part.headers["X-Custom"] == "hello"
*/
TEST(MultiFormTest, CustomHeaders)
{
    const std::string boundary = "hdrbound";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"meta\"\r\n";
    oss << "X-Custom: hello\r\n";
    oss << "X-Score: 42\r\n";
    oss << "\r\n";
    oss << "some data\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    const auto& part = form.at("meta");
    ASSERT_TRUE(part.headers.count("X-Custom"));
    EXPECT_EQ(part.headers.at("X-Custom"), "hello");
    ASSERT_TRUE(part.headers.count("X-Score"));
    EXPECT_EQ(part.headers.at("X-Score"), "42");
}

/*
测试思路：
1. boundary 行在 boundary 后可以有空白。
2. final boundary 也可能写成 "--boundary  --"。
3. parser 应跳过这些空白并识别 final boundary。

示例：
  --wsbound  \r\n ... --wsbound  --\r\n -> f=v
*/
TEST(MultiFormTest, BoundaryWithTrailingWhitespace)
{
    const std::string boundary = "wsbound";
    std::ostringstream oss;
    oss << "--" << boundary << "  \r\n";
    oss << "Content-Disposition: form-data; name=\"f\"\r\n";
    oss << "\r\n";
    oss << "v\r\n";
    oss << "--" << boundary << "  --\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("f"), 1u);
    EXPECT_EQ(PartText(form.at("f")), "v");
}

/*
测试思路：
1. Content-Disposition 不含 name 的 part 无法映射为字段。
2. MultiForm::addPart 会跳过 name 为空的 part。
3. 该用例固定“无名 part 不入索引”的行为。

示例：
  Content-Disposition: form-data -> form.empty()
*/
TEST(MultiFormTest, PartWithoutNameSkipped)
{
    const std::string boundary = "nonameb";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data\r\n";
    oss << "\r\n";
    oss << "orphan data\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    EXPECT_TRUE(form.empty());
}

/*
测试思路：
1. part Content-Type 值前后可能带空白。
2. ParseMultiformPartContentType 会裁剪 media type 并识别格式。
3. 原始 header 值仍保留在 raw_content_type 中，供必要时排查。

示例：
  Content-Type:   text/plain -> media_type=text/plain
*/
TEST(MultiFormTest, ContentTypeWithLeadingTrailingWhitespace)
{
    const std::string boundary = "ctws";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"f\"; filename=\"a.txt\"\r\n";
    oss << "Content-Type:   text/plain   \r\n";
    oss << "\r\n";
    oss << "hello\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    const auto& part = form.at("f");
    EXPECT_EQ(part.meta.raw_content_type, "   text/plain   ");
    EXPECT_EQ(part.meta.media_type, "text/plain");
    EXPECT_EQ(part.meta.known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ResolveContentCodecFormat(part.meta), ContentCodecFormat::kText);
}

/*
测试思路：
1. part 声明了 Content-Type 但值为空时，按 multipart part 缺省语义处理。
2. 第一版不保存额外布尔字段，缺省可通过 raw_content_type.empty() 判断。
3. format 应为 kPlainText，media_type 应为 text/plain。

示例：
  Content-Type:\r\n -> raw_content_type.empty(), format=kPlainText
*/
TEST(MultiFormTest, PartContentTypeEmptyFallsBackToPlainText)
{
    const std::string boundary = "emptyct";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"f\"\r\n";
    oss << "Content-Type:\r\n";
    oss << "\r\n";
    oss << "data\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    const auto& part = form.at("f");
    EXPECT_TRUE(part.meta.raw_content_type.empty());
    EXPECT_EQ(part.meta.media_type, "text/plain");
    EXPECT_EQ(part.meta.known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ResolveContentCodecFormat(part.meta), ContentCodecFormat::kText);
}

/*
测试思路：
1. filename 可能包含空格和括号等普通特殊字符。
2. parser 应在引号内完整提取，不截断。
3. 该用例覆盖 Content-Disposition 参数解析。

示例：
  filename="test file (1).pdf" -> same value
*/
TEST(MultiFormTest, FilenameWithSpecialChars)
{
    const std::string boundary = "specfn";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"file\"; filename=\"test file (1).pdf\"\r\n";
    oss << "\r\n";
    oss << "pdf content\r\n";
    oss << "--" << boundary << "--\r\n";

    const auto form = ParseStringBody(oss.str(), boundary);

    ASSERT_EQ(form.count("file"), 1u);
    EXPECT_EQ(form.at("file").filename, "test file (1).pdf");
}

/*
测试思路：
1. FormPart::toContentView 是 part 级 decode 复用 HTTP ContentDecoder 的适配点。
2. ContentView 应指向 part.data，size 与 meta 都保持一致。
3. 后续 DecodeMultiPartHelper 依赖该适配不丢信息。

示例：
  part.data="hello", meta=text/plain -> view.size==5, view.meta.kPlainText
*/
TEST(MultiFormTest, FormPartToContentViewPreservesBytesAndMeta)
{
    const std::string boundary = "view-boundary";
    const auto body = MakeSimpleBody(boundary, "field", "hello");
    const auto form = ParseStringBody(body, boundary);

    const auto& part = form.at("field");
    const auto view = part.toContentView();

    ASSERT_EQ(view.size, part.data.size());
    ASSERT_NE(view.data, nullptr);
    EXPECT_EQ(std::memcmp(view.data, part.data.data(), part.data.size()), 0);
    EXPECT_EQ(view.meta.known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ResolveContentCodecFormat(view.meta), ContentCodecFormat::kText);
    EXPECT_EQ(view.meta.media_type, "text/plain");
}
