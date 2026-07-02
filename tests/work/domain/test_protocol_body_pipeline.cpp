/**
 * @file test_protocol_body_pipeline.cpp
 * @brief 协议 Body 类型与字节内容匹配校验测试
 */

#include "domain/protocol.h"
#include "domain/protocol_body_pipeline.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

using namespace kit_domain;

namespace {

std::vector<char> Body(std::initializer_list<char> chars)
{
    return std::vector<char>(chars);
}

std::vector<char> Body(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

ProtocolBodyCheckResult Check(ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    return ProtocolBodyPipeline::CheckBody(ProtocolBodySpec{
        .body_type = body_type,
        .body_data = body_data,
    });
}

Protocol MakeFullBodyProtocol(ProtocolBodyType req_type,
                              const std::vector<char> &req_body,
                              ProtocolBodyType resp_type,
                              const std::vector<char> &resp_body)
{
    Protocol protocol;
    protocol.m_type = ProtocolType::kHttp;
    protocol.m_reqBodyType = req_type;
    protocol.m_reqBodyData = req_body;
    protocol.m_respBodyType = resp_type;
    protocol.m_respBodyData = resp_body;
    return protocol;
}

} // namespace

/**
 * 测试思路：
 * 1. 空 body 是业务允许的“未配置 body”状态，不应进入具体 JSON/XML/text 解析。
 * 2. 对 json/xml/text/binary 四种合法类型都传入空字节数组，pipeline 应统一返回成功。
 * 3. 该用例固定 ProtocolBodyPolicy 模板方法的空 body 分支，避免把空内容误判为非法 JSON/XML。
 *
 * 示例：
 *
 *   body_type=json + body_data=[]
 *        |
 *        v
 *   check ok
 */
TEST(TestProtocolBodyPipeline, EmptyBodyIsAllowedForEveryConcreteBodyType)
{
    const std::vector<char> empty;

    EXPECT_TRUE(Check(ProtocolBodyType::kJson, empty).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kXml, empty).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kText, empty).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kBinary, empty).ok);
}

/**
 * 测试思路：
 * 1. JSON body 的规则是“非空内容必须是完整合法 JSON 值”。
 * 2. 覆盖 object、array、string、number、bool、null，避免实现只允许 object。
 * 3. 这些值都能被 nlohmann::json 完整接受，pipeline 应返回成功。
 *
 * 示例：
 *
 *   body_type=json + body_data="[]"
 *        |
 *        v
 *   check ok
 */
TEST(TestProtocolBodyPipeline, JsonBodyAcceptsAnyCompleteJsonValue)
{
    EXPECT_TRUE(Check(ProtocolBodyType::kJson, Body(R"({})")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kJson, Body(R"([])")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kJson, Body(R"("abc")")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kJson, Body(R"(123)")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kJson, Body(R"(true)")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kJson, Body(R"(null)")).ok);
}

/**
 * 测试思路：
 * 1. JSON body 非空时必须被严格解析。
 * 2. 输入缺右花括号的 JSON，nlohmann::json::accept 应返回 false。
 * 3. pipeline 应失败并给出 json body invalid，防止坏 JSON 落库或进入 runtime。
 *
 * 示例：
 *
 *   body_type=json + body_data="{\"a\":"
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolBodyPipeline, JsonBodyRejectsBrokenJson)
{
    const auto result = Check(ProtocolBodyType::kJson, Body(R"({"a":)"));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("json body invalid"), std::string::npos);
}

/**
 * 测试思路：
 * 1. XML body 非空时必须通过真实 XML 结构校验。
 * 2. 一个单根节点且标签闭合正确的 XML 应被接受。
 * 3. 该用例避免 XML policy 停留在“未实现”或简单字符串包含判断。
 *
 * 示例：
 *
 *   body_type=xml + body_data="<root><a>1</a></root>"
 *        |
 *        v
 *   check ok
 */
TEST(TestProtocolBodyPipeline, XmlBodyAcceptsSingleWellFormedDocument)
{
    const auto result = Check(ProtocolBodyType::kXml, Body("<root><a>1</a></root>"));

    EXPECT_TRUE(result.ok) << result.message;
}

/*
 * 测试思路：
 * 1. XML body 必须拒绝结构错误的数据。
 * 2. 覆盖未正确闭合和多根节点两类常见错误。
 * 3. 这些内容不能以 text 或 binary 的宽松规则绕过 XML 校验。
 *
 * 示例：
 *
 *   body_type=xml + body_data="<root><a></root>"
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolBodyPipeline, XmlBodyRejectsMalformedOrMultiRootDocument)
{
    EXPECT_FALSE(Check(ProtocolBodyType::kXml, Body("<root><a></root>")).ok);
    EXPECT_FALSE(Check(ProtocolBodyType::kXml, Body("<a></a><b></b>")).ok);
}

/**
 * 测试思路：
 * 1. text body 非空时必须是合法 UTF-8 文本。
 * 2. UTF-8 合法性由 base/util.cpp 中的 IsUtf8Safe 统一判断。
 * 3. ASCII、中文 UTF-8，以及业务允许的空白控制符 \r\n\t 都应被接受。
 *
 * 示例：
 *
 *   body_type=text + body_data="hello\n中文"
 *        |
 *        v
 *   check ok
 */
TEST(TestProtocolBodyPipeline, TextBodyAcceptsUtf8AndAllowedWhitespace)
{
    EXPECT_TRUE(Check(ProtocolBodyType::kText, Body("hello world")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kText, Body(u8"中文文本🙂")).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kText, Body("line1\r\n\tline2")).ok);
}

/**
 * 测试思路：
 * 1. text body 在 UTF-8 合法之外仍保留业务规则：拒绝 NUL。
 * 2. NUL 字节容易截断 C 字符串语义，即使它本身是合法 UTF-8 码点也不能作为文本保存。
 * 3. pipeline 应优先给出稳定的 nul byte 错误，避免 NUL 被公共 UTF-8 校验放行。
 *
 * 示例：
 *
 *   body_type=text + body_data=['a', '\0', 'b']
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolBodyPipeline, TextBodyRejectsNulBeforeUtf8Validation)
{
    const auto result = Check(ProtocolBodyType::kText, Body({'a', '\0', 'b'}));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("nul byte"), std::string::npos);
}

/**
 * 测试思路：
 * 1. text body 除了 \t、\n、\r，不允许其它 C0 控制字符。
 * 2. 0x01 是合法单字节 UTF-8，但不是业务允许的可读文本字符。
 * 3. pipeline 应在调用公共 UTF-8 校验前拒绝它，保留原有文本业务规则。
 *
 * 示例：
 *
 *   body_type=text + body_data=['a', 0x01, 'b']
 *        |
 *        v
 *   check failed: invalid control character
 */
TEST(TestProtocolBodyPipeline, TextBodyRejectsDisallowedControlCharacter)
{
    const auto result = Check(ProtocolBodyType::kText, Body({'a', static_cast<char>(0x01), 'b'}));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("invalid control character"), std::string::npos);
}

/**
 * 测试思路：
 * 1. text body 的 UTF-8 合法性不再使用本文件手写状态机，而是交给 IsUtf8Safe。
 * 2. 输入 0xC3 0x28 是典型非法 UTF-8 序列，且不涉及业务控制字符规则。
 * 3. pipeline 应返回包含 valid utf-8 的错误，证明非法编码仍会被拒绝。
 *
 * 示例：
 *
 *   body_type=text + body_data=[0xC3, 0x28]
 *        |
 *        v
 *   check failed: valid utf-8
 */
TEST(TestProtocolBodyPipeline, TextBodyRejectsInvalidUtf8BySharedValidator)
{
    const auto result = Check(
        ProtocolBodyType::kText,
        Body({static_cast<char>(0xC3), static_cast<char>(0x28)}));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("valid utf-8"), std::string::npos);
}

/**
 * 测试思路：
 * 1. binary body 是原始字节透传，不应该套用 JSON/XML/text 规则。
 * 2. 输入包含 NUL、非法 UTF-8 和不可打印字节的数据。
 * 3. pipeline 应返回成功，保证 TCP 二进制 payload 不被错误过滤。
 *
 * 示例：
 *
 *   body_type=binary + body_data=[0x00, 0xff, 0x01]
 *        |
 *        v
 *   check ok
 */
TEST(TestProtocolBodyPipeline, BinaryBodyAcceptsAnyBytes)
{
    const auto result = Check(
        ProtocolBodyType::kBinary,
        Body({'\0', static_cast<char>(0xFF), static_cast<char>(0xC3), static_cast<char>(0x28)}));

    EXPECT_TRUE(result.ok) << result.message;
}

/**
 * 测试思路：
 * 1. kUnknown 和枚举上界外的值都不是可保存的业务 body type。
 * 2. pipeline 应在进入 policy 前失败。
 * 3. 该用例固定 enum 边界校验，避免未知类型绕过到默认策略。
 *
 * 示例：
 *
 *   body_type=unknown + body_data="{}"
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolBodyPipeline, UnknownOrOutOfRangeBodyTypeFails)
{
    EXPECT_FALSE(Check(ProtocolBodyType::kUnknown, Body("{}")).ok);
    EXPECT_FALSE(Check(static_cast<ProtocolBodyType>(static_cast<int>(ProtocolBodyType::kMax)), Body("{}")).ok);
}

/**
 * 测试思路：
 * 1. ProtocolBodyType::kBinary 对外字符串必须统一为 "binary"。
 * 2. 同时覆盖 nlohmann JSON 序列化、反序列化和 ProtocolBodyTypeToString 三个出口。
 * 3. 该用例防止旧拼写再次进入 Web 响应、请求解析或前端 payload。
 *
 * 示例：
 *
 *   ProtocolBodyType::kBinary
 *        |
 *        v
 *   "binary"
 */
TEST(TestProtocolBodyPipeline, BinaryBodyTypeUsesBinaryStringEverywhere)
{
    nlohmann::json encoded = ProtocolBodyType::kBinary;
    EXPECT_EQ(encoded, "binary");

    const auto decoded = nlohmann::json("binary").get<ProtocolBodyType>();
    EXPECT_EQ(decoded, ProtocolBodyType::kBinary);

    EXPECT_EQ(ProtocolBodyTypeToString(ProtocolBodyType::kBinary), "binary");
}

/**
 * 测试思路：
 * 1. checkFullProtocol 应分别校验 request body 和 response body。
 * 2. request 合法、response 非法时必须失败，并把错误前缀标成 response body invalid。
 * 3. 该用例防止实现误把 request body 校验两遍，导致 response body 漏检。
 *
 * 示例：
 *
 *   req=json "{}" + resp=json "{\"bad\":"
 *        |
 *        v
 *   response body invalid
 */
TEST(TestProtocolBodyPipeline, FullProtocolChecksResponseBodyIndependently)
{
    auto protocol = MakeFullBodyProtocol(
        ProtocolBodyType::kJson,
        Body("{}"),
        ProtocolBodyType::kJson,
        Body(R"({"bad":)"));

    const auto result = ProtocolBodyPipeline::CheckFullProtocol(protocol);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("response body invalid"), std::string::npos);
}

/**
 * 测试思路：
 * 1. checkFullProtocol 应优先报告 request body 的错误。
 * 2. request 非法、response 合法时失败信息应带 request body invalid。
 * 3. 该用例给 runtime add/reconfig 写入口提供明确的错误定位语义。
 *
 * 示例：
 *
 *   req=json "{\"bad\":" + resp=json "{}"
 *        |
 *        v
 *   request body invalid
 */
TEST(TestProtocolBodyPipeline, FullProtocolReportsRequestBodyInvalidPrefix)
{
    auto protocol = MakeFullBodyProtocol(
        ProtocolBodyType::kJson,
        Body(R"({"bad":)"),
        ProtocolBodyType::kJson,
        Body("{}"));

    const auto result = ProtocolBodyPipeline::CheckFullProtocol(protocol);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("request body invalid"), std::string::npos);
}
