/**
 * @file test_protocol_body_pipeline.cpp
 * @brief 协议 Body 类型与字节内容匹配校验测试
 */

#include "domain/protocol.h"
#include "domain/protocol_body_pipeline.h"
#include "domain/protocol_item.h"
#include "domain/custom_tcp_field_model.h"
#include "net/http/multiform.h"

#include "gtest/gtest.h"

#include <string>
#include <utility>
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
    EXPECT_TRUE(Check(ProtocolBodyType::kMultiForm, empty).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kImage, empty).ok);
    EXPECT_TRUE(Check(ProtocolBodyType::kBinary, empty).ok);
}

/**
 * 测试思路：
 * 1. kEmpty 表示协议项明确配置了“空 body”，和其它 body 类型的空字节兼容规则不同。
 * 2. 空字节应通过；非空字节必须失败，避免配置为空时仍发送实际内容。
 * 3. kNone 表示“不关心 body”，即使收到非空字节也应直接通过，不进入具体 policy。
 *
 * 示例：
 *
 *   kEmpty + []     -> check ok
 *   kEmpty + "data" -> check failed
 *   kNone  + "data" -> check ok
 */
TEST(TestProtocolBodyPipeline, EmptyAndNoneBodyTypesKeepDistinctSemantics)
{
    EXPECT_TRUE(Check(ProtocolBodyType::kEmpty, {}).ok);

    const auto empty_body_result = Check(ProtocolBodyType::kEmpty, Body("data"));
    EXPECT_FALSE(empty_body_result.ok);
    EXPECT_NE(empty_body_result.message.find("body not empty"), std::string::npos);

    EXPECT_TRUE(Check(ProtocolBodyType::kNone, Body("data")).ok);
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
 * 1. Binary Body 的配置内容不再是裸字节，而是由 fields[].spec 与 fields[].value
 *    组成的字段 JSON；pipeline 必须接受前端写出的完整嵌套结构。
 * 2. spec.match 描述字段规格，value 描述实际响应字节。两者都采用 H 前缀十六进制
 *    字符串，字段 role 固定为 common。
 * 3. 该用例固定配置保存入口，避免 Binary policy 退回到只校验“任意内容都通过”。
 *
 * 示例：
 *
 *   body_type=binary + {fields:[{spec:{type:UINT32,...}, value:"H05060708"}]}
 *        |
 *        v
 *   check ok
 */
TEST(TestProtocolBodyPipeline, BinaryBodyAcceptsNestedFieldValueConfiguration)
{
    const nlohmann::json binary_config = {
        {"fields", nlohmann::json::array({
            {
                {"spec", {
                    {"byte_len", 4},
                    {"byte_pos", 0},
                    {"match", "HFFFFFFFF"},
                    {"name", "响应标识"},
                    {"role", "common"},
                    {"type", "UINT32"},
                }},
                {"value", "H05060708"},
            },
        })},
    };

    const auto result = Check(ProtocolBodyType::kBinary, Body(binary_config.dump()));

    EXPECT_TRUE(result.ok) << result.message;
}

/**
 * 测试思路：
 * 1. BinaryBodyPolicy 的职责是提前阻止不满足嵌套 fields/spec 契约的内容进入运行态。
 * 2. 只含 value 的旧扁平字段、以及不是 JSON 的��二进制内容都缺少 spec，必须失败。
 * 3. 这样运行态 ProtocolItemBodyView 可以假设输入已经完成基础结构校验。
 *
 * 示例：
 *
 *   {"fields":[{"value":"H05060708"}]} -> check failed
 *   [0x00, 0xFF]                         -> check failed
 */
TEST(TestProtocolBodyPipeline, BinaryBodyRejectsMissingSpecOrRawBytes)
{
    const nlohmann::json missing_spec = {
        {"fields", nlohmann::json::array({{{"value", "H05060708"}}})},
    };

    const auto missing_spec_result = Check(
        ProtocolBodyType::kBinary, Body(missing_spec.dump()));
    EXPECT_FALSE(missing_spec_result.ok);
    EXPECT_NE(missing_spec_result.message.find("binary body invalid"), std::string::npos);

    const auto raw_bytes_result = Check(
        ProtocolBodyType::kBinary,
        Body({'\0', static_cast<char>(0xFF), static_cast<char>(0xC3), static_cast<char>(0x28)}));
    EXPECT_FALSE(raw_bytes_result.ok);
    EXPECT_NE(raw_bytes_result.message.find("binary body invalid"), std::string::npos);
}

/**
 * 测试思路：
 * 1. parser 需要把字段的定义与写入值分离保存：spec.match 不能覆盖外层 value。
 * 2. 断言解析出的 map 以 byte_pos 为键、总字节数来自 byte_len，且默认采用大端。
 * 3. 使用不同的 match/value 防止实现退化为只从 spec.match 读取响应字节。
 *
 * 示例：
 *
 *   spec.match=HFFFFFFFF, value=H05060708
 *                 |
 *                 v
 *   map[0].spec.match == FFFFFFFF, map[0].bytes == 05 06 07 08
 */
TEST(TestProtocolBodyPipeline, BinaryFieldValueMapKeepsSpecAndOuterValueSeparate)
{
    const nlohmann::json binary_config = {
        {"fields", nlohmann::json::array({
            {
                {"spec", {
                    {"byte_len", 4},
                    {"byte_pos", 0},
                    {"match", "HFFFFFFFF"},
                    {"name", "响应标识"},
                    {"role", "common"},
                    {"type", "UINT32"},
                }},
                {"value", "H05060708"},
            },
        })},
    };

    const auto [fields, total_len] = FieldValueMapParseFromJson(binary_config);

    ASSERT_EQ(total_len, 4U);
    ASSERT_EQ(fields.size(), 1U);
    const auto field_it = fields.find(0U);
    ASSERT_NE(field_it, fields.end());
    EXPECT_EQ(field_it->second.spec.name, "响应标识");
    EXPECT_EQ(field_it->second.spec.byte_len, 4U);
    EXPECT_EQ(field_it->second.spec.byte_order, FieldByteOrder::kBigEndian);
    ASSERT_TRUE(field_it->second.spec.match.has_value());
    EXPECT_EQ(*field_it->second.spec.match,
              (std::vector<uint8_t>{0xFF, 0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(field_it->second.bytes,
              (std::vector<uint8_t>{0x05, 0x06, 0x07, 0x08}));
}

/**
 * 测试思路：
 * 1. 运行态响应 Body 要按字段 byte_pos 将外层 value 写入连续字节流。
 * 2. 配置两个相邻字段，覆盖多字段排序和总长度计算；只断言四个配置字节，
 *    防止旧实现预分配后 insert 导致末尾多出四个 0x00。
 * 3. spec.match 故意与 value 不同，确认发送内容只由 value 决定。
 *
 * 示例：
 *
 *   pos=0, len=2, value=H0102  +  pos=2, len=2, value=H0304
 *                                  |
 *                                  v
 *   runtime response = [01 02 03 04]，长度恰为 4
 */
TEST(TestProtocolBodyPipeline, BinaryBodyViewAssemblesExactlyConfiguredResponseBytes)
{
    const nlohmann::json binary_config = {
        {"fields", nlohmann::json::array({
            {
                {"spec", {
                    {"byte_len", 2},
                    {"byte_pos", 0},
                    {"match", "HFFFF"},
                    {"name", "起始"},
                    {"role", "common"},
                    {"type", "UINT16"},
                }},
                {"value", "H0102"},
            },
            {
                {"spec", {
                    {"byte_len", 2},
                    {"byte_pos", 2},
                    {"match", "HFFFF"},
                    {"name", "状态"},
                    {"role", "common"},
                    {"type", "UINT16"},
                }},
                {"value", "H0304"},
            },
        })},
    };

    ProtocolItemBodyView view(
        ProtocolBodyType::kBinary,
        Body(binary_config.dump()));

    ASSERT_NE(view.body_data, nullptr);
    EXPECT_EQ(view.body_type, ProtocolBodyType::kBinary);
    EXPECT_EQ(view.meta.known_type, kit_muduo::http::KnownMediaType::kApplicationOctetStream);
    EXPECT_EQ(view.meta.media_type, "application/octet-stream");
    EXPECT_EQ(*view.body_data, Body({0x01, 0x02, 0x03, 0x04}));
}

/*
测试思路：
1. Body 转换可能在 JSON 解析或 multipart 构造阶段失败。
2. setBody 必须先完成局部转换，再提交 body_type/meta/body_data，避免留下半更新快照。
3. 旧快照保持完整，运行态更新失败时才能继续使用旧配置。

示例：
  old text body -> setBody(invalid binary) -> 仍保持 old text body
*/
TEST(TestProtocolBodyPipeline, BodyViewKeepsPreviousSnapshotWhenConversionFails)
{
    ProtocolItemBodyView view(ProtocolBodyType::kText, Body("previous"));

    EXPECT_THROW(
        view.setBody(ProtocolBodyType::kBinary, Body("not-json")),
        std::exception);

    EXPECT_EQ(view.body_type, ProtocolBodyType::kText);
    EXPECT_EQ(view.meta.known_type, kit_muduo::http::KnownMediaType::kTextPlain);
    EXPECT_EQ(view.meta.media_type, "text/plain");
    ASSERT_NE(view.body_data, nullptr);
    EXPECT_EQ(*view.body_data, Body("previous"));
}

/*
测试思路：
1. Multiform 配置在保存或运行态更新前必须先完成 JSON/字段结构校验。
2. 合法 fields 描述允许进入运行态，非法 JSON 不应等到 HTTP 请求命中时才失败。
*/
TEST(TestProtocolBodyPipeline, MultiFormBodyValidatesConfiguredFields)
{
    const auto valid = Body(R"({"fields":[{"name":"message","type":"text","value":"ok"}]})");
    const auto invalid = Body(R"({"fields":{}})");

    EXPECT_TRUE(Check(ProtocolBodyType::kMultiForm, valid).ok);
    const auto invalid_result = Check(ProtocolBodyType::kMultiForm, invalid);
    EXPECT_FALSE(invalid_result.ok);
    EXPECT_NE(invalid_result.message.find("multiform body invalid"), std::string::npos);
}

/*
测试思路：
1. ProtocolItemBodyView 初始化时应完成 Multiform 配置到 wire body 的转换，并缓存 ContentMeta。
2. 响应侧不再由 HttpProjectServer 在命中请求时重复编码；这里直接回读缓存 body 验证 boundary 和字段。
3. 空配置也应生成合法的 final boundary，而不是留下没有 boundary 的 multipart 响应。
*/
TEST(TestProtocolBodyPipeline, MultiFormBodyViewCachesWireBodyAndMetadata)
{
    constexpr const char *kBoundary = "KitProtocolFormBoundary";
    const auto config = Body(
        R"({"fields":[{"name":"message","type":"text","value":"hello"}]})");

    ProtocolItemBodyView view(ProtocolBodyType::kMultiForm, config);
    EXPECT_EQ(view.meta.known_type, kit_muduo::http::KnownMediaType::kMultipartFormData);
    EXPECT_EQ(view.meta.media_type, "multipart/form-data");
    const auto *boundary = kit_muduo::http::GetContentTypeParam(view.meta, "boundary");
    ASSERT_NE(boundary, nullptr);
    EXPECT_EQ(*boundary, kBoundary);

    ASSERT_NE(view.body_data, nullptr);
    const auto form = kit_muduo::http::MultiForm::parse(
        reinterpret_cast<const uint8_t *>(view.body_data->data()),
        view.body_data->size(),
        kBoundary);
    EXPECT_EQ(form.at("message").strs(), "hello");

    ProtocolItemBodyView empty_view(ProtocolBodyType::kMultiForm, {});
    ASSERT_NE(empty_view.body_data, nullptr);
    const auto empty_form = kit_muduo::http::MultiForm::parse(
        reinterpret_cast<const uint8_t *>(empty_view.body_data->data()),
        empty_view.body_data->size(),
        kBoundary);
    EXPECT_TRUE(empty_form.empty());
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
    EXPECT_TRUE(Check(ProtocolBodyType::kNone, Body("{}")).ok);
    EXPECT_FALSE(Check(static_cast<ProtocolBodyType>(static_cast<int>(ProtocolBodyType::kMax)), Body("{}")).ok);
}

/**
 * 测试思路：
 * 1. 本次协议体扩展新增 none、empty、multiform、image 字符串值。
 * 2. 每个值都要在配置解析、响应序列化和领域字符串 helper 之间保持一致。
 * 3. 逐项往返可以防止只修改某一个出口，导致数据库或 Web 接口出现隐式回退。
 *
 * 示例：
 *
 *   "multiform" -> kMultiForm -> "multiform"
 */
TEST(TestProtocolBodyPipeline, NewBodyTypesRoundTripThroughExternalStrings)
{
    const std::vector<std::pair<ProtocolBodyType, std::string>> cases = {
        {ProtocolBodyType::kNone, "none"},
        {ProtocolBodyType::kEmpty, "empty"},
        {ProtocolBodyType::kMultiForm, "multiform"},
        {ProtocolBodyType::kImage, "image"},
    };

    for(const auto &[body_type, encoded] : cases)
    {
        SCOPED_TRACE(encoded);
        EXPECT_EQ(ProtocolBodyTypeFromString(encoded), body_type);
        EXPECT_EQ(ProtocolBodyTypeToString(body_type), encoded);
    }
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
