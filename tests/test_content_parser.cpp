/**
 * @file test_content_parser.cpp
 * @brief MultiPartParser 专项测试
 * @author Kewin Li
 * @date 2026-05-06
 */
#include "gtest/gtest.h"
#include "test_log.h"

#include "base/content_parser.h"

#include <cstring>
#include <sstream>

using namespace kit_muduo;

namespace {

/// 辅助函数：构造仅含单个文本字段的最小 multipart body。
std::string make_simple_body(const std::string& boundary,
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

/// 辅助函数：构造指定 boundary 的 Content-Type 头值。
std::string make_ct(const std::string& boundary)
{
    return "multipart/form-data; boundary=" + boundary;
}

} // namespace

// ==================================================================
// 一、Boundary 提取
// ==================================================================

/*
 * 测试思路：传入空字符串作为 Content-Type —— 无法提取 boundary。
 *
 * 输入示例：
 *   data:  "--abc\r\n"
 *   ct:    ""                    ← 空 Content-Type
 *
 * 预期：返回空 PartMap，不崩溃。
 */
TEST(MultiPartParser, NoContentType_ReturnsEmpty)
{
    auto parts = MultiFormParser::parse("--abc\r\n", 6, "");
    EXPECT_TRUE(parts.empty());
}

/*
 * 测试思路：Content-Type 不含 "boundary=" 键 —— 无法提取 boundary。
 *
 * 输入示例：
 *   data:  "--abc\r\n"
 *   ct:    "multipart/form-data"  ← 缺少 boundary= 参数
 *
 * 预期：返回空 PartMap。
 */
TEST(MultiPartParser, NoBoundaryInContentType_ReturnsEmpty)
{
    auto parts = MultiFormParser::parse("--abc\r\n", 6, "multipart/form-data");
    EXPECT_TRUE(parts.empty());
}

/*
 * 测试思路：boundary 值被双引号包裹 —— 正确提取引号内的值。
 *
 * 输入示例：
 *   data:  "--AaB03x\r\nContent-Disposition: form-data; name=\"field1\"\r\n\r\nhello\r\n--AaB03x--\r\n"
 *   ct:    "multipart/form-data; boundary=\"AaB03x\""
 *
 * 预期：提取 boundary = "AaB03x"，成功解析出 field1 = "hello"。
 */
TEST(MultiPartParser, QuotedBoundary_Extracted)
{
    const std::string boundary = "AaB03x";
    auto body = make_simple_body(boundary, "field1", "hello");
    auto parts = MultiFormParser::parse(body, "multipart/form-data; boundary=\"" + boundary + "\"");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts["field1"].name, "field1");
}

/*
 * 测试思路：boundary 值无引号 —— 按分号截断并去除首尾空白。
 *
 * 输入示例：
 *   data:  "--AaB03x\r\nContent-Disposition: form-data; name=\"field1\"\r\n\r\nhello\r\n--AaB03x--\r\n"
 *   ct:    "multipart/form-data; boundary=AaB03x"
 *
 * 预期：提取 boundary = "AaB03x"，成功解析。
 */
TEST(MultiPartParser, UnquotedBoundary_Extracted)
{
    const std::string boundary = "AaB03x";
    auto body = make_simple_body(boundary, "field1", "hello");
    auto parts = MultiFormParser::parse(body, "multipart/form-data; boundary=" + boundary);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts["field1"].name, "field1");
}

// ==================================================================
// 二、基本字段解析
// ==================================================================

/*
 * 测试思路：标准 HTML 表单提交单个文本字段。
 *
 * 输入示例（boundary = "boundary123"）：
 *   --boundary123
 *   Content-Disposition: form-data; name="username"
 *
 *   john
 *   --boundary123--
 *
 * 预期：PartMap 有 1 个条目，key = "username"，data = "john"，
 *       is_file() = false，filename 为空。
 */
TEST(MultiPartParser, SingleTextField)
{
    const std::string boundary = "boundary123";
    auto body = make_simple_body(boundary, "username", "john");

    auto parts = MultiFormParser::parse(body, make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_TRUE(parts.count("username"));
    const auto& part = parts["username"];
    EXPECT_EQ(part.name, "username");
    EXPECT_EQ(part.filename, "");
    EXPECT_FALSE(part.is_file());
    EXPECT_EQ(std::string(part.data.begin(), part.data.end()), "john");
}

/*
 * 测试思路：一个 multipart body 包含 3 个字段。
 *
 * 输入示例（boundary = "b0"）：
 *   --b0
 *   Content-Disposition: form-data; name="a"
 *
 *   1
 *   --b0
 *   Content-Disposition: form-data; name="b"
 *
 *   2
 *   --b0
 *   Content-Disposition: form-data; name="c"
 *
 *   3
 *   --b0--
 *
 * 预期：PartMap 有 3 个条目，各自 data 为 "1"、"2"、"3"。
 */
TEST(MultiPartParser, MultipleTextFields)
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

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(std::string(parts["a"].data.begin(), parts["a"].data.end()), "1");
    EXPECT_EQ(std::string(parts["b"].data.begin(), parts["b"].data.end()), "2");
    EXPECT_EQ(std::string(parts["c"].data.begin(), parts["c"].data.end()), "3");
}

/*
 * 测试思路：同一 name 出现两次（如多选 checkbox），后者覆盖前者。
 *
 * 输入示例（boundary = "b0"）：
 *   --b0
 *   Content-Disposition: form-data; name="dup"
 *
 *   first
 *   --b0
 *   Content-Disposition: form-data; name="dup"
 *
 *   second
 *   --b0--
 *
 * 预期：PartMap 有 1 个条目（非 2 个），data = "second"。
 *       这与 HTML 表单语义一致：浏览器提交时后出现的值覆盖先出现的值。
 */
TEST(MultiPartParser, SameNameTwice_LastWins)
{
    const std::string boundary = "b0";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"dup\"\r\n\r\n";
    oss << "first\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"dup\"\r\n\r\n";
    oss << "second\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(std::string(parts["dup"].data.begin(), parts["dup"].data.end()), "second");
}

// ==================================================================
// 三、文件上传
// ==================================================================

/*
 * 测试思路：模拟文件上传 —— Content-Disposition 带 filename，
 *           同时有 Content-Type 描述文件 MIME 类型，body 为伪二进制内容。
 *
 * 输入示例（boundary = "fileboundary"）：
 *   --fileboundary
 *   Content-Disposition: form-data; name="avatar"; filename="photo.png"
 *   Content-Type: image/png
 *
 *   <89>PNG\r\nfake_data
 *   --fileboundary--
 *
 * 预期：name = "avatar"，filename = "photo.png"，content_type = "image/png"，
 *       is_file() = true，data 保全所有字节（包含 \x89 等非 ASCII 字符）。
 */
TEST(MultiPartParser, FileUpload)
{
    const std::string boundary = "fileboundary";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"avatar\"; filename=\"photo.png\"\r\n";
    oss << "Content-Type: image/png\r\n";
    oss << "\r\n";
    oss << "\x89PNG\r\nfake_data";
    oss << "\r\n--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    const auto& part = parts["avatar"];
    EXPECT_EQ(part.name, "avatar");
    EXPECT_EQ(part.filename, "photo.png");
    EXPECT_EQ(part.content_type, "image/png");
    EXPECT_TRUE(part.is_file());
    EXPECT_EQ(std::string(part.data.begin(), part.data.end()), std::string("\x89PNG\r\nfake_data"));
}

// ==================================================================
// 四、二进制安全
// ==================================================================

/*
 * 测试思路：body 内嵌 '\0' 空字节 —— 旧 string 方案可能截断；
 *           新方案按 size_t 长度处理，空字节不得丢失。
 *
 * 输入示例（boundary = "binbound"）：
 *   --binbound
 *   Content-Disposition: form-data; name="bin"
 *
 *   H e \0 l l \0 o             ← 7 字节，含 2 个空字节
 *   --binbound--
 *
 * 预期：part.data.size() == 7，memcmp 全等。
 */
TEST(MultiPartParser, BinaryBodyWithNullBytes)
{
    const std::string boundary = "binbound";
    const char binary_data[] = {'H', 'e', '\0', 'l', 'l', '\0', 'o'};
    const std::string bin_str(binary_data, sizeof(binary_data));

    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"bin\"\r\n";
    oss << "\r\n";
    oss.write(binary_data, sizeof(binary_data));
    oss << "\r\n--" << boundary << "--\r\n";

    const std::string body = oss.str();
    auto parts = MultiFormParser::parse(body.data(), body.size(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    const auto& part = parts["bin"];
    ASSERT_EQ(part.data.size(), sizeof(binary_data));
    EXPECT_EQ(std::memcmp(part.data.data(), binary_data, sizeof(binary_data)), 0);
}

/*
 * 测试思路：上传文件的二进制内容中恰好存在与 boundary 模式相似的字节序列。
 *           例如文件内包含 "\r\n--myboundaryXY" 这样的字节，
 *           其中 "\r\n--myboundary" 前缀和真实 boundary 完全一致。
 *
 *           旧方案直接 find("--boundary") 会在 body 内部误匹配。
 *           新方案要求 boundary 后面必须是合法后缀字符
 *           （\r, \n, -, 空格, \t），因此 "XY" 不合法，会被跳过。
 *
 * 输入示例（boundary = "myboundary"）：
 *   --myboundary
 *   Content-Disposition: form-data; name="safe"
 *
 *   \r\n--myboundaryXY          ← 17 字节 body，以 "\r\n--myboundary" 开头但后缀为 "XY"
 *   --myboundary--
 *
 * 预期：body 完整保全 17 字节，不被误切割为 0 字节。
 */
TEST(MultiPartParser, BodyContainsBoundaryLikePattern)
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
    auto parts = MultiFormParser::parse(body.data(), body.size(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    const auto& part = parts["safe"];
    ASSERT_EQ(part.data.size(), sizeof(pattern));
    EXPECT_EQ(std::memcmp(part.data.data(), pattern, sizeof(pattern)), 0);
}

// ==================================================================
// 五、边界情况
// ==================================================================

/*
 * 测试思路：字段值为空 —— headers 和 boundary 之间只有一个空行。
 *
 * 输入示例（boundary = "emptyb"）：
 *   --emptyb
 *   Content-Disposition: form-data; name="empty"
 *
 *                                ← 空行，表示 body 长度为 0
 *   --emptyb--
 *
 * 预期：part 存在，name = "empty"，data.empty() == true。
 */
TEST(MultiPartParser, EmptyFieldValue)
{
    const std::string boundary = "emptyb";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"empty\"\r\n";
    oss << "\r\n";
    oss << "\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_TRUE(parts["empty"].data.empty());
}

/*
 * 测试思路：第一个 boundary 从 body 的第 0 个字节开始（无 preamble）。
 *           这是最常见的 multipart 格式。
 *
 * 输入示例（boundary = "startb"）：
 *   --startb                     ← 第 0 字节就是 "--"
 *   Content-Disposition: form-data; name="f"
 *
 *   v
 *   --startb--
 *
 * 预期：正常解析，field "f" = "v"。
 */
TEST(MultiPartParser, NoPreamble_BoundaryAtStart)
{
    const std::string boundary = "startb";
    auto body = make_simple_body(boundary, "f", "v");

    auto parts = MultiFormParser::parse(body, make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(std::string(parts["f"].data.begin(), parts["f"].data.end()), "v");
}

/*
 * 测试思路：第一个 boundary 之前有 preamble（前导文本）。
 *           按 RFC 2046，preamble 应被忽略。
 *
 * 输入示例（boundary = "pb"）：
 *   This is preamble text that should be ignored
 *   --pb
 *   Content-Disposition: form-data; name="field"
 *
 *   value
 *   --pb--
 *
 * 预期：preamble 被忽略，field = "value" 正常解析。
 */
TEST(MultiPartParser, PreambleIgnored)
{
    const std::string boundary = "pb";
    std::ostringstream oss;
    oss << "This is preamble text that should be ignored\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"field\"\r\n";
    oss << "\r\n";
    oss << "value\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(std::string(parts["field"].data.begin(), parts["field"].data.end()), "value");
}

/*
 * 测试思路：关闭 boundary 之后还有 epilogue（尾部文本）。
 *           按 RFC 2046，epilogue 应被忽略。
 *
 * 输入示例（boundary = "epb"）：
 *   --epb
 *   Content-Disposition: form-data; name="f"
 *
 *   v
 *   --epb--
 *   This is epilogue text to be ignored    ← 关闭 boundary 之后的文本
 *
 * 预期：epilogue 被忽略，field "f" 正常解析。
 */
TEST(MultiPartParser, EpilogueIgnored)
{
    const std::string boundary = "epb";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"f\"\r\n";
    oss << "\r\n";
    oss << "v\r\n";
    oss << "--" << boundary << "--\r\n";
    oss << "This is epilogue text to be ignored\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
}

/*
 * 测试思路：body 只有一个关闭 boundary，没有任何 part。
 *
 * 输入示例（boundary = "onlyclose"）：
 *   --onlyclose--
 *
 * 预期：返回空 PartMap，不崩溃。
 */
TEST(MultiPartParser, OnlyFinalBoundary_NoParts)
{
    const std::string boundary = "onlyclose";
    std::ostringstream oss;
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    EXPECT_TRUE(parts.empty());
}

/*
 * 测试思路：body 完全不包含 boundary 标记 —— 非法 multipart 报文。
 *
 * 输入示例：
 *   data:  "just some text, no boundary here"
 *   ct:    "multipart/form-data; boundary=b0"
 *
 * 预期：返回空 PartMap，不崩溃。
 */
TEST(MultiPartParser, MissingBoundaryMarker)
{
    auto parts = MultiFormParser::parse("just some text, no boundary here", make_ct("b0"));
    EXPECT_TRUE(parts.empty());
}

// ==================================================================
// 六、行尾兼容（仅 LF 无 CR）
// ==================================================================

/*
 * 测试思路：报文使用 LF（\n）而非 CRLF（\r\n）作为行尾。
 *           RFC 虽规定 CRLF，但实际存在仅用 LF 的客户端。
 *           新的解析方案搜索 "\n--boundary"，天然兼容两种行尾。
 *
 * 输入示例（boundary = "lfboundary"）：
 *   --lfboundary\n
 *   Content-Disposition: form-data; name="f"\n
 *   \n
 *   value\n
 *   --lfboundary--\n
 *
 * 预期：正常解析，field "f" = "value"。
 */
TEST(MultiPartParser, LfOnlyLineEndings)
{
    const std::string boundary = "lfboundary";
    std::ostringstream oss;
    oss << "--" << boundary << "\n";
    oss << "Content-Disposition: form-data; name=\"f\"\n";
    oss << "\n";
    oss << "value\n";
    oss << "--" << boundary << "--\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(std::string(parts["f"].data.begin(), parts["f"].data.end()), "value");
}

// ==================================================================
// 七、自定义 headers
// ==================================================================

/*
 * 测试思路：part 中包含非标准 header（如 X-Custom、X-Score）。
 *           解析器应将 Content-Disposition 和 Content-Type 以外的
 *           header 存入 FormPart::headers map。
 *
 * 输入示例（boundary = "hdrbound"）：
 *   --hdrbound
 *   Content-Disposition: form-data; name="meta"
 *   X-Custom: hello
 *   X-Score: 42
 *
 *   some data
 *   --hdrbound--
 *
 * 预期：part.headers["X-Custom"] = "hello"，
 *       part.headers["X-Score"] = "42"。
 */
TEST(MultiPartParser, CustomHeaders)
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

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    const auto& part = parts["meta"];
    ASSERT_TRUE(part.headers.count("X-Custom"));
    EXPECT_EQ(part.headers.at("X-Custom"), "hello");
    ASSERT_TRUE(part.headers.count("X-Score"));
    EXPECT_EQ(part.headers.at("X-Score"), "42");
}

// ==================================================================
// 八、Boundary 后缀空白
// ==================================================================

/*
 * 测试思路：boundary 行在 "--boundary" 和 CRLF 之间带可选 LWSP
 *          （空格 / 水平制表符）。RFC 2046 §5.1.1 允许此类空白。
 *           关闭 boundary 同样可以带空白："--boundary  --"。
 *
 * 输入示例（boundary = "wsbound"）：
 *   --wsbound  \r\n              ← boundary 后有两个空格
 *   Content-Disposition: form-data; name="f"
 *
 *   v
 *   --wsbound  --\r\n            ← 关闭 boundary，"  --" 前有两个空格
 *
 * 预期：正常解析，field "f" = "v"。
 */
TEST(MultiPartParser, BoundaryWithTrailingWhitespace)
{
    const std::string boundary = "wsbound";
    std::ostringstream oss;
    oss << "--" << boundary << "  \r\n";
    oss << "Content-Disposition: form-data; name=\"f\"\r\n";
    oss << "\r\n";
    oss << "v\r\n";
    oss << "--" << boundary << "  --\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(std::string(parts["f"].data.begin(), parts["f"].data.end()), "v");
}

// ==================================================================
// 九、无 name 的 part 会被跳过
// ==================================================================

/*
 * 测试思路：Content-Disposition 不含 name 属性。
 *           这种 part 无法映射到具体字段名，应被跳过。
 *
 * 输入示例（boundary = "nonameb"）：
 *   --nonameb
 *   Content-Disposition: form-data      ← 没有 name="..."
 *
 *   orphan data
 *   --nonameb--
 *
 * 预期：返回空 PartMap。
 */
TEST(MultiPartParser, PartWithoutName_Skipped)
{
    const std::string boundary = "nonameb";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data\r\n";
    oss << "\r\n";
    oss << "orphan data\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    EXPECT_TRUE(parts.empty());
}

// ==================================================================
// 十、Content-Type 空白裁剪
// ==================================================================

/*
 * 测试思路：part 的 Content-Type header 值前后带有空白字符。
 *           parse_part 中的 trim 应去除这些空白。
 *
 * 输入示例（boundary = "ctws"）：
 *   --ctws
 *   Content-Disposition: form-data; name="f"; filename="a.txt"
 *   Content-Type:   text/plain         ← "   text/plain   " 首尾有空白
 *
 *   hello
 *   --ctws--
 *
 * 预期：part.content_type = "text/plain"（已 trim）。
 */
TEST(MultiPartParser, ContentTypeWithLeadingTrailingWhitespace)
{
    const std::string boundary = "ctws";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"f\"; filename=\"a.txt\"\r\n";
    oss << "Content-Type:   text/plain   \r\n";
    oss << "\r\n";
    oss << "hello\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts["f"].content_type, "text/plain");
}

// ==================================================================
// 十一、Part 的 Content-Type 为空
// ==================================================================

/*
 * 测试思路：part 声明了 Content-Type header 但未提供值。
 *           此时 trim 应返回空串，不崩溃。
 *
 * 输入示例（boundary = "emptyct"）：
 *   --emptyct
 *   Content-Disposition: form-data; name="f"
 *   Content-Type:                ← 值为空
 *
 *   data
 *   --emptyct--
 *
 * 预期：part.content_type.empty() == true。
 */
TEST(MultiPartParser, PartContentTypeEmpty)
{
    const std::string boundary = "emptyct";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"f\"\r\n";
    oss << "Content-Type:\r\n";
    oss << "\r\n";
    oss << "data\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_TRUE(parts["f"].content_type.empty());
}

// ==================================================================
// 十二、文件名含特殊字符
// ==================================================================

/*
 * 测试思路：上传文件的 filename 包含空格和括号等特殊字符。
 *           Content-Disposition 用双引号包裹值，解析器应在引号内
 *           完整提取文件名，不做额外转义或截断。
 *
 * 输入示例（boundary = "specfn"）：
 *   --specfn
 *   Content-Disposition: form-data; name="file"; filename="test file (1).pdf"
 *
 *   pdf content
 *   --specfn--
 *
 * 预期：part.filename = "test file (1).pdf"（含空格和括号）。
 */
TEST(MultiPartParser, FilenameWithSpecialChars)
{
    const std::string boundary = "specfn";
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"file\"; filename=\"test file (1).pdf\"\r\n";
    oss << "\r\n";
    oss << "pdf content\r\n";
    oss << "--" << boundary << "--\r\n";

    auto parts = MultiFormParser::parse(oss.str(), make_ct(boundary));

    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts["file"].filename, "test file (1).pdf");
}
