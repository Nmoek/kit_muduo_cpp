/**
 * @file test_protocol_interaction.cpp
 * @brief 协议项实时交互详情领域模型与 payload 捕获测试
 */

#include "base/util.h"
#include "domain/protocol_interaction.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace kit_domain;

namespace {

std::vector<uint8_t> Bytes(const std::string &text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

InteractionPayloadHint Hint(ProtocolType protocol_type,
                            ProtocolBodyType expect_body_type,
                            std::string media_type = {},
                            bool prefer_hex_text_for_binary = false)
{
    return InteractionPayloadHint{
        .protocol_type = protocol_type,
        .expect_body_type = expect_body_type,
        .media_type = std::move(media_type),
        .prefer_hex_text_for_binary = prefer_hex_text_for_binary,
    };
}

InteractionCaptureOptions Options(size_t max_text_bytes = 64 * 1024,
                                  size_t max_hex_bytes = 64 * 1024,
                                  size_t max_attachment_bytes = 10 * 1024 * 1024)
{
    return InteractionCaptureOptions{
        .max_text_bytes = max_text_bytes,
        .max_hex_bytes = max_hex_bytes,
        .max_binary_attachment_bytes = max_attachment_bytes,
    };
}

} // namespace

/**
 * 测试思路：
 * 1. 手工构造一条能归属到协议项的正常交互记录。
 * 2. 转成 JSON 后，必须包含顶层通用字段、request、response。
 * 3. raw_packet 未设置时不输出，进程内 binary_sidecars 也不能进入 JSON 契约。
 *
 * 示例：
 *
 *   ProtocolInteractionRecord(scope=protocol, protocol_id=12)
 *        |
 *        v
 *   json["request"] + json["response"] 存在
 *   json 不包含 raw_packet / binary_sidecars
 */
TEST(TestProtocolInteraction, ProtocolRecordJsonContainsContractFieldsAndOmitsPrivateData)
{
    ProtocolInteractionRecord record;
    record.seq = 101;
    record.scope = InteractionScope::kProtocol;
    record.project_id = 1;
    record.protocol_id = 12;
    record.protocol_type = ProtocolType::kHttp;
    record.time_ms = 1780000000000;
    record.peer_addr = "127.0.0.1:53001";
    record.result = InteractionResult::kMatched;

    record.request.meta = {
        {"method", "POST"},
        {"target", "/upload?debug=1"},
        {"path", "/upload"},
        {"version", "HTTP/1.1"},
    };
    record.request.head_text = "POST /upload?debug=1 HTTP/1.1\r\n\r\n";
    record.request.body.kind = InteractionPayloadKind::kJson;
    record.request.body.expect_kind = InteractionPayloadKind::kJson;
    record.request.body.size = 11;
    record.request.body.captured_size = 11;
    record.request.body.sha1 = "request-sha1";
    record.request.body.text = R"({"ok":true})";

    record.response.meta = {
        {"version", "HTTP/1.1"},
        {"status_code", 200},
    };
    record.response.head_text = "HTTP/1.1 200 OK\r\n\r\n";
    record.response.body.kind = InteractionPayloadKind::kText;
    record.response.body.expect_kind = InteractionPayloadKind::kText;
    record.response.body.size = 2;
    record.response.body.captured_size = 2;
    record.response.body.sha1 = "response-sha1";
    record.response.body.text = "ok";

    record.binary_sidecars.push_back(BinarySidecar{
        .attachment_ref = InteractionAttachmentRef{.attachment_id = "private"},
        .bytes = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{1, 2, 3}),
    });

    nlohmann::json json = record;

    EXPECT_EQ(json["seq"], 101);
    EXPECT_EQ(json["scope"], "protocol");
    EXPECT_EQ(json["project_id"], 1);
    EXPECT_EQ(json["protocol_id"], 12);
    EXPECT_EQ(json["protocol_type"], "HTTP");
    EXPECT_EQ(json["time_ms"], 1780000000000);
    EXPECT_EQ(json["peer_addr"], "127.0.0.1:53001");
    EXPECT_EQ(json["result"], "matched");
    EXPECT_EQ(json["error_message"], "");

    ASSERT_TRUE(json.contains("request"));
    ASSERT_TRUE(json.contains("response"));
    EXPECT_EQ(json["request"]["meta"]["method"], "POST");
    EXPECT_EQ(json["request"]["head_text"], "POST /upload?debug=1 HTTP/1.1\r\n\r\n");
    EXPECT_EQ(json["request"]["body"]["kind"], "json");
    EXPECT_EQ(json["request"]["body"]["expect_kind"], "json");
    EXPECT_EQ(json["response"]["meta"]["status_code"], 200);
    EXPECT_EQ(json["response"]["body"]["text"], "ok");

    EXPECT_FALSE(json["request"].contains("raw_packet"));
    EXPECT_FALSE(json["response"].contains("raw_packet"));
    EXPECT_FALSE(json.contains("binary_sidecars"));
}

/**
 * 测试思路：
 * 1. 构造项目级异常 notice，模拟同项目内不能归属到具体协议项的事件。
 * 2. 这类记录必须使用 scope=project，并且 protocol_id 固定为 0。
 * 3. result/error_message 要稳定进入 JSON，供前端显示 route not found 等异常。
 *
 * 示例：
 *
 *   HTTP 路由未命中
 *        |
 *        v
 *   ProtocolInteractionRecord(scope=project, protocol_id=0, result=route_not_found)
 */
TEST(TestProtocolInteraction, ProjectNoticeJsonUsesProjectScopeAndProtocolIdZero)
{
    ProtocolInteractionRecord notice;
    notice.seq = 102;
    notice.scope = InteractionScope::kProject;
    notice.project_id = 1;
    notice.protocol_id = 0;
    notice.protocol_type = ProtocolType::kHttp;
    notice.result = InteractionResult::kRouteNotFound;
    notice.error_message = "route not found";
    notice.request.meta = {{"method", "GET"}, {"path", "/missing"}};
    notice.request.head_text = "GET /missing HTTP/1.1\r\n\r\n";

    nlohmann::json json = notice;

    EXPECT_EQ(json["scope"], "project");
    EXPECT_EQ(json["project_id"], 1);
    EXPECT_EQ(json["protocol_id"], 0);
    EXPECT_EQ(json["result"], "route_not_found");
    EXPECT_EQ(json["error_message"], "route not found");
    EXPECT_EQ(json["request"]["meta"]["path"], "/missing");
}

/**
 * 测试思路：
 * 1. 已经可靠拆出 body，且协议项期望 JSON。
 * 2. body 是合法 UTF-8 和合法 JSON 时，实际展示类型保持 json。
 * 3. 可读内容进入 body.text，不生成附件，也不占用 sidecar bytes。
 *
 * 示例：
 *
 *   expect=json + bytes="{\"ok\":true}"
 *        |
 *        v
 *   body.kind=json, body.text=原文, sidecars=[]
 */
TEST(TestProtocolInteraction, ValidJsonBodyIsReadableTextWithoutAttachment)
{
    const auto data = Bytes(R"({"ok":true})");
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kJson, "application/json"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);

    EXPECT_EQ(body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(body.size, data.size());
    EXPECT_EQ(body.captured_size, data.size());
    EXPECT_FALSE(body.truncated);
    EXPECT_EQ(body.text, R"({"ok":true})");
    EXPECT_EQ(body.sha1, kit_muduo::Sha1BytesBase64Helper(data));
    EXPECT_TRUE(body.error_message.empty());
    EXPECT_TRUE(body.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
}

/**
 * 测试思路：
 * 1. body 边界可靠，但协议项期望 JSON 的内容本身格式错误。
 * 2. 这种错误不应该升级成 raw_packet；它仍然是一个可展示的 body。
 * 3. 实际展示类型降级为 text，原始安全文本保留，错误原因进入 error_message。
 *
 * 示例：
 *
 *   expect=json + bytes="{\"name\":\"abc\",}"
 *        |
 *        v
 *   body.kind=text, body.text=原文, body.error_message!=空
 */
TEST(TestProtocolInteraction, BrokenJsonBodyFallsBackToTextAndDoesNotNeedRawPacket)
{
    const auto data = Bytes(R"({"name":"abc",})");
    std::vector<BinarySidecar> sidecars;

    InteractionSide side;
    side.body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kJson, "application/json"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);

    nlohmann::json json = side;

    EXPECT_EQ(side.body.kind, InteractionPayloadKind::kText);
    EXPECT_EQ(side.body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(side.body.size, data.size());
    EXPECT_EQ(side.body.captured_size, data.size());
    EXPECT_FALSE(side.body.truncated);
    EXPECT_EQ(side.body.text, R"({"name":"abc",})");
    EXPECT_NE(side.body.error_message.find("json data invalid"), std::string::npos);
    EXPECT_TRUE(side.body.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
    EXPECT_FALSE(json.contains("raw_packet"));
}

/**
 * 测试思路：
 * 1. XML 和 text 都属于可读 body，前提是字节序列是安全 UTF-8。
 * 2. XML 会额外做格式校验；普通 text 只要求安全文本。
 * 3. 两者都应直接进入 body.text，不生成附件。
 *
 * 示例：
 *
 *   expect=xml  + "<root/>"  -> kind=xml
 *   expect=text + "line1"    -> kind=text
 */
TEST(TestProtocolInteraction, XmlAndPlainTextBodiesUseReadableTextPath)
{
    const auto xml_data = Bytes("<root><id>1</id></root>");
    const auto text_data = Bytes("line1\nline2");
    std::vector<BinarySidecar> sidecars;

    const auto xml_body = InteractionBody::BuildFromBytes(
        xml_data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kXml, "application/xml"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);
    const auto text_body = InteractionBody::BuildFromBytes(
        text_data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kText, "text/plain"),
        ProtocolSide::kResponse,
        Options(),
        sidecars);

    EXPECT_EQ(xml_body.kind, InteractionPayloadKind::kXml);
    EXPECT_EQ(xml_body.expect_kind, InteractionPayloadKind::kXml);
    EXPECT_EQ(xml_body.text, "<root><id>1</id></root>");
    EXPECT_TRUE(xml_body.attachments.empty());

    EXPECT_EQ(text_body.kind, InteractionPayloadKind::kText);
    EXPECT_EQ(text_body.expect_kind, InteractionPayloadKind::kText);
    EXPECT_EQ(text_body.text, "line1\nline2");
    EXPECT_TRUE(text_body.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
}

/**
 * 测试思路：
 * 1. 协议项期望 text，但实际收到非法 UTF-8。
 * 2. 非法文本不能直接放进 JSON 字符串字段，否则前端和 JSON 编码都有风险。
 * 3. 捕获逻辑要降级为 binary，并把十六进制前缀放入 body.text。
 *
 * 示例：
 *
 *   expect=text + bytes=[0x61, 0xC3, 0x28]
 *        |
 *        v
 *   body.kind=binary, body.text="H61 C3", truncated=true
 */
TEST(TestProtocolInteraction, InvalidUtf8TextFallsBackToBinaryHexPrefix)
{
    const std::vector<uint8_t> data{0x61, 0xC3, 0x28};
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kText, "text/plain"),
        ProtocolSide::kRequest,
        Options(64 * 1024, 2, 10 * 1024 * 1024),
        sidecars);

    EXPECT_EQ(body.kind, InteractionPayloadKind::kBinary);
    EXPECT_EQ(body.expect_kind, InteractionPayloadKind::kText);
    EXPECT_EQ(body.size, 3);
    EXPECT_EQ(body.captured_size, 2);
    EXPECT_TRUE(body.truncated);
    EXPECT_EQ(body.text, "H61 C3");
    EXPECT_NE(body.error_message.find("utf-8 invalid"), std::string::npos);
    EXPECT_TRUE(body.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
}

/**
 * 测试思路：
 * 1. HTTP 二进制 body 不应直接塞进 JSON 正文。
 * 2. Content-Type 是 image/png 时，实际展示类型应识别为 image。
 * 3. 未截断附件需要同时生成 attachments[] 元数据和 sidecar bytes。
 *
 * 示例：
 *
 *   HTTP Content-Type=image/png + bytes=[PNG...]
 *        |
 *        v
 *   body.kind=image, attachments[0].binary_available=true, sidecars[0].bytes=原始 bytes
 */
TEST(TestProtocolInteraction, HttpImageBodyBuildsAttachmentAndSidecarBytes)
{
    const std::vector<uint8_t> data{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kBinary, "image/png; charset=binary"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);

    ASSERT_EQ(body.attachments.size(), 1);
    const auto &ref = body.attachments.front();

    EXPECT_EQ(body.kind, InteractionPayloadKind::kImage);
    EXPECT_EQ(body.expect_kind, InteractionPayloadKind::kBinary);
    EXPECT_EQ(body.size, data.size());
    EXPECT_EQ(body.captured_size, data.size());
    EXPECT_FALSE(body.truncated);
    EXPECT_TRUE(body.text.empty());

    EXPECT_EQ(ref.side, "request");
    EXPECT_EQ(ref.flag, "request.body");
    EXPECT_EQ(ref.kind, InteractionPayloadKind::kImage);
    EXPECT_EQ(ref.size, data.size());
    EXPECT_EQ(ref.captured_size, data.size());
    EXPECT_FALSE(ref.truncated);
    EXPECT_TRUE(ref.binary_available);
    EXPECT_EQ(ref.sha1, body.sha1);
    EXPECT_EQ(ref.attachment_id, "request.body:" + body.sha1.substr(0, 16));

    ASSERT_EQ(sidecars.size(), 1);
    EXPECT_EQ(sidecars.front().attachment_ref.attachment_id, ref.attachment_id);
    ASSERT_NE(sidecars.front().bytes, nullptr);
    EXPECT_EQ(*sidecars.front().bytes, data);
}

/**
 * 测试思路：
 * 1. 附件型 body 超过捕获上限时，JSON 仍要保留附件元数据。
 * 2. 由于当前 sidecar 只发送完整未截断 bytes，truncated=true 时不应生成 sidecar。
 * 3. 前端可以据此显示“已截断/不可下载”的状态，而不是误以为附件丢失。
 *
 * 示例：
 *
 *   max_binary_attachment_bytes=3 + pdf bytes size=5
 *        |
 *        v
 *   attachments[0].captured_size=3, binary_available=false, sidecars=[]
 */
TEST(TestProtocolInteraction, TruncatedAttachmentKeepsMetadataWithoutSidecarBytes)
{
    const std::vector<uint8_t> data{'%', 'P', 'D', 'F', '-'};
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kBinary, "application/pdf"),
        ProtocolSide::kResponse,
        Options(64 * 1024, 64 * 1024, 3),
        sidecars);

    ASSERT_EQ(body.attachments.size(), 1);
    const auto &ref = body.attachments.front();

    EXPECT_EQ(body.kind, InteractionPayloadKind::kPdf);
    EXPECT_EQ(body.size, 5);
    EXPECT_EQ(body.captured_size, 3);
    EXPECT_TRUE(body.truncated);
    EXPECT_EQ(ref.side, "response");
    EXPECT_EQ(ref.flag, "response.body");
    EXPECT_EQ(ref.kind, InteractionPayloadKind::kPdf);
    EXPECT_EQ(ref.size, 5);
    EXPECT_EQ(ref.captured_size, 3);
    EXPECT_TRUE(ref.truncated);
    EXPECT_FALSE(ref.binary_available);
    EXPECT_TRUE(sidecars.empty());
}

/**
 * 测试思路：
 * 1. CustomTcp v1 的 body 主展示形态是十六进制文本。
 * 2. prefer_hex_text_for_binary=true 时，不走 HTTP 附件分发路径。
 * 3. 太大的 body 只展示 hex 前缀，并通过 truncated 标记说明被截断。
 *
 * 示例：
 *
 *   TCP bytes=[0x01,0x02,0xAB,0xCD], max_hex_bytes=3
 *        |
 *        v
 *   body.text="H01 02 AB", attachments=[], sidecars=[]
 */
TEST(TestProtocolInteraction, CustomTcpBinaryBodyUsesHexTextWithoutAttachment)
{
    const std::vector<uint8_t> data{0x01, 0x02, 0xAB, 0xCD};
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kCustomTcp, ProtocolBodyType::kBinary, {}, true),
        ProtocolSide::kRequest,
        Options(64 * 1024, 3, 10 * 1024 * 1024),
        sidecars);

    EXPECT_EQ(body.kind, InteractionPayloadKind::kBinary);
    EXPECT_EQ(body.expect_kind, InteractionPayloadKind::kBinary);
    EXPECT_EQ(body.size, 4);
    EXPECT_EQ(body.captured_size, 3);
    EXPECT_TRUE(body.truncated);
    EXPECT_EQ(body.text, "H01 02 AB");
    EXPECT_TRUE(body.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
}

/**
 * 测试思路：
 * 1. parser error 场景无法可靠拆出 head/body，需要用 raw_packet 表达整包前缀。
 * 2. raw_packet 的 raw_hex 受 max_hex_bytes 控制，但 sha1 仍基于完整原始 bytes。
 * 3. raw_packet 有值时，InteractionSide JSON 必须输出 raw_packet 字段。
 *
 * 示例：
 *
 *   raw bytes=['B','A','D',0x01,0x02], max_hex_bytes=4
 *        |
 *        v
 *   raw_hex="H42 41 44 01", truncated=true, json["raw_packet"] 存在
 */
TEST(TestProtocolInteraction, RawPacketCapturesHexPrefixAndSerializesWhenPresent)
{
    const std::vector<uint8_t> data{'B', 'A', 'D', 0x01, 0x02};
    std::vector<BinarySidecar> sidecars;

    InteractionSide side;
    side.raw_packet = InteractionRawPacket::BuildRawPacketFromBytes(
        data,
        ProtocolSide::kRequest,
        Options(64 * 1024, 4, 10 * 1024 * 1024),
        sidecars);

    nlohmann::json json = side;

    ASSERT_TRUE(side.raw_packet.has_value());
    EXPECT_EQ(side.raw_packet->kind, InteractionPayloadKind::kBinary);
    EXPECT_EQ(side.raw_packet->size, 5);
    EXPECT_EQ(side.raw_packet->captured_size, 4);
    EXPECT_TRUE(side.raw_packet->truncated);
    EXPECT_EQ(side.raw_packet->raw_hex, "H42 41 44 01");
    EXPECT_EQ(side.raw_packet->sha1, kit_muduo::Sha1BytesBase64Helper(data));
    EXPECT_TRUE(side.raw_packet->attachments.empty());
    EXPECT_TRUE(sidecars.empty());

    ASSERT_TRUE(json.contains("raw_packet"));
    EXPECT_EQ(json["raw_packet"]["kind"], "binary");
    EXPECT_EQ(json["raw_packet"]["raw_hex"], "H42 41 44 01");
    EXPECT_EQ(json["raw_packet"]["truncated"], true);
}
