/**
 * @file test_protocol_interaction.cpp
 * @brief 协议项实时交互详情领域模型与 payload 捕获测试
 */

#include "base/util.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/protocol_interaction_observation.h"
#include "domain/protocol_interaction_publisher.h"

#include "gtest/gtest.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

InteractionRecord MakeRecord(uint64_t seq,
                                     InteractionScope scope,
                                     int64_t project_id,
                                     int64_t protocol_id,
                                     InteractionResult result = InteractionResult::kMatched)
{
    InteractionRecord record;
    record.seq = seq;
    record.scope = scope;
    record.project_id = project_id;
    record.protocol_id = protocol_id;
    record.protocol_type = ProtocolType::kHttp;
    record.time_ms = 1780000000000 + static_cast<int64_t>(seq);
    record.peer_addr = "127.0.0.1:53001";
    record.result = result;
    return record;
}

class CollectingInteractionSink : public InteractionSink
{
public:
    void publish(InteractionRecord record) override
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            records_.push_back(std::move(record));
        }
        cv_.notify_all();
    }

    void clearProtocol(int64_t project_id, int64_t protocol_id) override
    {
        std::lock_guard<std::mutex> lock(mtx_);
        cleared_protocols_.push_back({project_id, protocol_id});
    }

    void clearProject(int64_t project_id) override
    {
        std::lock_guard<std::mutex> lock(mtx_);
        cleared_projects_.push_back(project_id);
    }

    bool WaitForRecordCount(size_t expected_count,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return cv_.wait_for(lock, timeout, [this, expected_count]() {
            return records_.size() >= expected_count;
        });
    }

    std::vector<InteractionRecord> Records() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return records_;
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<InteractionRecord> records_;
    std::vector<std::pair<int64_t, int64_t>> cleared_protocols_;
    std::vector<int64_t> cleared_projects_;
};

class BlockingInteractionSink : public InteractionSink
{
public:
    void publish(InteractionRecord record) override
    {
        std::unique_lock<std::mutex> lock(mtx_);
        records_.push_back(std::move(record));
        cv_.notify_all();

        if(records_.size() == 1U)
        {
            cv_.wait(lock, [this]() {
                return unblocked_;
            });
        }
    }

    void clearProtocol(int64_t, int64_t) override {}

    void clearProject(int64_t) override {}

    bool WaitForRecordCount(size_t expected_count,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return cv_.wait_for(lock, timeout, [this, expected_count]() {
            return records_.size() >= expected_count;
        });
    }

    void Unblock()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            unblocked_ = true;
        }
        cv_.notify_all();
    }

    std::vector<InteractionRecord> Records() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return records_;
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<InteractionRecord> records_;
    bool unblocked_{false};
};

class ThrowingInteractionSink : public InteractionSink
{
public:
    void publish(InteractionRecord) override
    {
        ++publish_count_;
        throw std::runtime_error("sink publish failed");
    }

    void clearProtocol(int64_t, int64_t) override {}

    void clearProject(int64_t) override {}

    int publishCount() const
    {
        return publish_count_.load();
    }

private:
    std::atomic_int publish_count_{0};
};

ProtocolInteractionObservation MakeHttpObservation(
    int64_t project_id,
    int64_t protocol_id,
    const std::string &path,
    const std::string &request_body = R"({"ok":true})",
    const std::string &response_body = R"({"accepted":true})")
{
    ProtocolInteractionObservation obs;
    obs.scope = InteractionScope::kProtocol;
    obs.project_id = project_id;
    obs.protocol_id = protocol_id;
    obs.protocol_type = ProtocolType::kHttp;
    obs.time_ms = 1780000010000 + protocol_id;
    obs.peer_addr = "127.0.0.1:53010";
    obs.result = InteractionResult::kMatched;
    obs.request.meta = {
        {"method", "POST"},
        {"path", path},
    };
    obs.request.head_text = "POST " + path + " HTTP/1.1\r\n"
        "Content-Type: application/json\r\n\r\n";
    obs.request.body_bytes = Bytes(request_body);
    obs.request.expect_body_type = ProtocolBodyType::kJson;
    obs.request.media_type = "application/json";
    obs.response.meta = {
        {"status_code", 200},
    };
    obs.response.head_text = "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n\r\n";
    obs.response.body_bytes = Bytes(response_body);
    obs.response.expect_body_type = ProtocolBodyType::kJson;
    obs.response.media_type = "application/json";
    return obs;
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
    InteractionRecord record;
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
    EXPECT_EQ(json["protocol_type"], "http");
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
    InteractionRecord notice;
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

/**
 * 测试思路：
 * 1. 先发布一条历史记录，让 Hub 的 currentSeq 前进到 10。
 * 2. 订阅 project=1/protocol=12 后，只应该收到订阅之后且协议项匹配的记录。
 * 3. 同项目但其他协议项、其他项目、订阅前序号的记录都不能推给该订阅者。
 *
 * 示例：
 *
 *   publish(seq=10, pc=12) -> subscribe(start=11)
 *        |
 *        +-- publish(seq=10, pc=12)  不推送
 *        +-- publish(seq=11, pc=13)  不推送
 *        +-- publish(seq=12, pc=12)  推送
 */
TEST(TestProtocolInteraction, HubOnlyPushesMatchingProtocolRecordsAfterSubscribe)
{
    ProtocolInteractionHub hub;
    hub.publish(MakeRecord(10, InteractionScope::kProtocol, 1, 12));
    ASSERT_EQ(hub.currentSeq(), 10U);

    std::vector<InteractionRecord> received;
    auto subscription = hub.subscribe(
        InteractionSubscribeFilter{
            .project_id = 1,
            .protocol_id = 12,
            .include_project_notice = false,
        },
        [&received](const InteractionRecord &record) {
            received.push_back(record);
        });

    ASSERT_NE(subscription.subscriber_id, 0U);
    EXPECT_EQ(subscription.start_record_seq, 11U);

    hub.publish(MakeRecord(10, InteractionScope::kProtocol, 1, 12));
    hub.publish(MakeRecord(11, InteractionScope::kProtocol, 1, 13));
    hub.publish(MakeRecord(12, InteractionScope::kProtocol, 2, 12));
    hub.publish(MakeRecord(13, InteractionScope::kProtocol, 1, 12));

    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received.front().seq, 13U);
    EXPECT_EQ(received.front().project_id, 1);
    EXPECT_EQ(received.front().protocol_id, 12);
}

/**
 * 测试思路：
 * 1. 建立两个订阅者：一个只看协议项记录，一个同时包含项目级 notice。
 * 2. 发布同项目 route_not_found 这类项目级记录时，只有 include_project_notice=true 的订阅者收到。
 * 3. 取消订阅后，再发布匹配协议项记录，被取消订阅者不应继续收到。
 *
 * 示例：
 *
 *   protocol-only subscriber     + project-notice subscriber
 *        |                                  |
 *   publish(scope=project)             只第二个收到
 *   unsubscribe(second)
 *   publish(scope=protocol)            只有第一个收到
 */
TEST(TestProtocolInteraction, HubProjectNoticeRequiresOptInAndUnsubscribeStopsDelivery)
{
    ProtocolInteractionHub hub;
    std::vector<InteractionRecord> protocol_only_records;
    std::vector<InteractionRecord> with_notice_records;

    auto protocol_only = hub.subscribe(
        InteractionSubscribeFilter{
            .project_id = 1,
            .protocol_id = 12,
            .include_project_notice = false,
        },
        [&protocol_only_records](const InteractionRecord &record) {
            protocol_only_records.push_back(record);
        });

    auto with_notice = hub.subscribe(
        InteractionSubscribeFilter{
            .project_id = 1,
            .protocol_id = 12,
            .include_project_notice = true,
        },
        [&with_notice_records](const InteractionRecord &record) {
            with_notice_records.push_back(record);
        });

    hub.publish(MakeRecord(
        1,
        InteractionScope::kProject,
        1,
        0,
        InteractionResult::kRouteNotFound));

    EXPECT_TRUE(protocol_only_records.empty());
    ASSERT_EQ(with_notice_records.size(), 1U);
    EXPECT_EQ(with_notice_records.front().scope, InteractionScope::kProject);
    EXPECT_EQ(with_notice_records.front().protocol_id, 0);
    EXPECT_EQ(with_notice_records.front().result, InteractionResult::kRouteNotFound);

    hub.unsubcribe(with_notice.subscriber_id);
    hub.publish(MakeRecord(2, InteractionScope::kProtocol, 1, 12));

    ASSERT_EQ(protocol_only_records.size(), 1U);
    EXPECT_EQ(protocol_only_records.front().seq, 2U);
    EXPECT_EQ(with_notice_records.size(), 1U);

    hub.unsubcribe(protocol_only.subscriber_id);
}

/**
 * 测试思路：
 * 1. 使用真实 Publisher 工作线程，把 Observation 转换成 Record 后投递到内存 Sink。
 * 2. 协议项 scope 的 protocol_id 必须保留，seq 从 1 开始递增。
 * 3. request/response 的 meta、head_text、body 类型和文本要从 Observation 稳定转换。
 *
 * 示例：
 *
 *   Observation(scope=protocol, protocol_id=12, body="{\"ok\":true}")
 *        |
 *        v
 *   Sink 收到 Record(seq=1, protocol_id=12, request.body.kind=json)
 */
TEST(TestProtocolInteraction, PublisherBuildsProtocolRecordAndDeliversToSink)
{
    auto sink = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();

    ProtocolInteractionObservation obs;
    obs.scope = InteractionScope::kProtocol;
    obs.project_id = 1;
    obs.protocol_id = 12;
    obs.protocol_type = ProtocolType::kHttp;
    obs.time_ms = 1780000000100;
    obs.peer_addr = "127.0.0.1:53001";
    obs.result = InteractionResult::kMatched;
    obs.request.meta = {
        {"method", "POST"},
        {"path", "/api/create"},
    };
    obs.request.head_text = "POST /api/create HTTP/1.1\r\nContent-Type: application/json\r\n\r\n";
    obs.request.body_bytes = Bytes(R"({"ok":true})");
    obs.request.expect_body_type = ProtocolBodyType::kJson;
    obs.request.media_type = "application/json";
    obs.response.meta = {
        {"status_code", 200},
    };
    obs.response.head_text = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
    obs.response.body_bytes = Bytes("ok");
    obs.response.expect_body_type = ProtocolBodyType::kText;
    obs.response.media_type = "text/plain";

    publisher.publish(std::move(obs));

    ASSERT_TRUE(sink->WaitForRecordCount(1));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.seq, 1U);
    EXPECT_EQ(publisher.CurrentSeq(), 1U);
    EXPECT_EQ(record.scope, InteractionScope::kProtocol);
    EXPECT_EQ(record.project_id, 1);
    EXPECT_EQ(record.protocol_id, 12);
    EXPECT_EQ(record.protocol_type, ProtocolType::kHttp);
    EXPECT_EQ(record.time_ms, 1780000000100);
    EXPECT_EQ(record.peer_addr, "127.0.0.1:53001");
    EXPECT_EQ(record.result, InteractionResult::kMatched);

    EXPECT_EQ(record.request.meta["method"], "POST");
    EXPECT_EQ(record.request.head_text, "POST /api/create HTTP/1.1\r\nContent-Type: application/json\r\n\r\n");
    EXPECT_EQ(record.request.body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.text, R"({"ok":true})");

    EXPECT_EQ(record.response.meta["status_code"], 200);
    EXPECT_EQ(record.response.body.kind, InteractionPayloadKind::kText);
    EXPECT_EQ(record.response.body.expect_kind, InteractionPayloadKind::kText);
    EXPECT_EQ(record.response.body.text, "ok");
}

/**
 * 测试思路：
 * 1. 项目级异常 Observation 没有具体协议项归属，即使上游误传 protocol_id 也不能泄漏到 Record。
 * 2. request.raw_bytes 要进入 raw_packet，方便前端查看无法解析的原始报文前缀。
 * 3. result/error_message 保持项目 notice 语义，供订阅端展示路由未命中等异常。
 *
 * 示例：
 *
 *   Observation(scope=project, protocol_id=99, raw="BAD")
 *        |
 *        v
 *   Record(scope=project, protocol_id=0, request.raw_packet.raw_hex="H42 41 44")
 */
TEST(TestProtocolInteraction, PublisherNormalizesProjectNoticeProtocolIdAndRawPacket)
{
    auto sink = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(64 * 1024, 3, 10 * 1024 * 1024),
        });

    publisher.start();

    ProtocolInteractionObservation obs;
    obs.scope = InteractionScope::kProject;
    obs.project_id = 1;
    obs.protocol_id = 99;
    obs.protocol_type = ProtocolType::kHttp;
    obs.time_ms = 1780000000200;
    obs.peer_addr = "127.0.0.1:53002";
    obs.result = InteractionResult::kRouteNotFound;
    obs.error_message = "route not found";
    obs.request.meta = {
        {"method", "GET"},
        {"path", "/missing"},
    };
    obs.request.raw_bytes = std::vector<uint8_t>{'B', 'A', 'D', 0x01};

    publisher.publish(std::move(obs));

    ASSERT_TRUE(sink->WaitForRecordCount(1));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.seq, 1U);
    EXPECT_EQ(record.scope, InteractionScope::kProject);
    EXPECT_EQ(record.project_id, 1);
    EXPECT_EQ(record.protocol_id, 0);
    EXPECT_EQ(record.result, InteractionResult::kRouteNotFound);
    EXPECT_EQ(record.error_message, "route not found");
    EXPECT_EQ(record.request.meta["path"], "/missing");
    ASSERT_TRUE(record.request.raw_packet.has_value());
    EXPECT_EQ(record.request.raw_packet->size, 4U);
    EXPECT_EQ(record.request.raw_packet->captured_size, 3U);
    EXPECT_TRUE(record.request.raw_packet->truncated);
    EXPECT_EQ(record.request.raw_packet->raw_hex, "H42 41 44");
}

/**
 * 测试思路：
 * 1. Publisher 是运行态异步发布管道，队列满时必须丢弃新 observation，而不是阻塞协议响应线程。
 * 2. 构造 queue_capacity=2，并让 sink 阻塞第一条记录，使 worker 暂时不能继续消费队列。
 * 3. 再连续 publish 三条 observation，第二、第三条进入队列，第四条触发队列满被丢弃。
 * 4. 放开 sink 后只应投递前三条记录，CurrentSeq 也只能推进到 3，避免丢弃数据错误占用 seq。
 *
 * 示例：
 *
 *   queue_capacity=2, worker blocked in sink("/first")
 *        + publish(path="/second")  -> queued
 *        + publish(path="/third")   -> queued
 *        + publish(path="/dropped") -> dropped
 *        v
 *   unblock sink -> sink 只收到 "/first"、"/second" 和 "/third"
 */
TEST(TestProtocolInteraction, PublisherDropsObservationWhenQueueFullWithoutAdvancingSeq)
{
    auto sink = std::make_shared<BlockingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 2,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    ProtocolInteractionObservation first;
    first.scope = InteractionScope::kProtocol;
    first.project_id = 1;
    first.protocol_id = 12;
    first.protocol_type = ProtocolType::kHttp;
    first.time_ms = 1780000000300;
    first.peer_addr = "127.0.0.1:53003";
    first.result = InteractionResult::kMatched;
    first.request.meta = {{"path", "/first"}};
    first.request.body_bytes = Bytes("first");
    first.request.expect_body_type = ProtocolBodyType::kText;
    first.request.media_type = "text/plain";

    ProtocolInteractionObservation second = first;
    second.time_ms = 1780000000400;
    second.request.meta = {{"path", "/second"}};
    second.request.body_bytes = Bytes("second");

    ProtocolInteractionObservation third = first;
    third.time_ms = 1780000000500;
    third.request.meta = {{"path", "/third"}};
    third.request.body_bytes = Bytes("third");

    ProtocolInteractionObservation dropped = first;
    dropped.time_ms = 1780000000600;
    dropped.request.meta = {{"path", "/dropped"}};
    dropped.request.body_bytes = Bytes("dropped");

    publisher.start();

    publisher.publish(std::move(first));
    ASSERT_TRUE(sink->WaitForRecordCount(1));
    publisher.publish(std::move(second));
    publisher.publish(std::move(third));
    publisher.publish(std::move(dropped));

    sink->Unblock();
    ASSERT_TRUE(sink->WaitForRecordCount(3));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0].seq, 1U);
    EXPECT_EQ(records[0].request.meta["path"], "/first");
    EXPECT_EQ(records[0].request.body.text, "first");
    EXPECT_EQ(records[1].seq, 2U);
    EXPECT_EQ(records[1].request.meta["path"], "/second");
    EXPECT_EQ(records[1].request.body.text, "second");
    EXPECT_EQ(records[2].seq, 3U);
    EXPECT_EQ(records[2].request.meta["path"], "/third");
    EXPECT_EQ(records[2].request.body.text, "third");
    EXPECT_EQ(publisher.CurrentSeq(), 3U);
}

/**
 * 测试思路：
 * 1. 连续发布三条 observation，真实 Publisher worker 负责转换并分配 record.seq。
 * 2. record.seq 必须在进程内单调递增，不能因为异步 worker 或 body 转换改变顺序。
 * 3. CurrentSeq() 最终应等于最后一条已发布 record 的 seq。
 *
 * 示例：
 *
 *   publish("/one"), publish("/two"), publish("/three")
 *        |
 *        v
 *   sink records seq = [1, 2, 3], CurrentSeq() = 3
 */
TEST(TestProtocolInteraction, PublisherAssignsMonotonicSeqForContinuousObservations)
{
    auto sink = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 8,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    publisher.publish(MakeHttpObservation(1, 12, "/one"));
    publisher.publish(MakeHttpObservation(1, 12, "/two"));
    publisher.publish(MakeHttpObservation(1, 12, "/three"));

    ASSERT_TRUE(sink->WaitForRecordCount(3));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[0].seq, 1U);
    EXPECT_EQ(records[0].request.meta["path"], "/one");
    EXPECT_EQ(records[1].seq, 2U);
    EXPECT_EQ(records[1].request.meta["path"], "/two");
    EXPECT_EQ(records[2].seq, 3U);
    EXPECT_EQ(records[2].request.meta["path"], "/three");
    EXPECT_EQ(publisher.CurrentSeq(), 3U);
}

/**
 * 测试思路：
 * 1. 第一条 observation 进入 sink 后阻塞，让 worker 暂停在 publishRecord 阶段。
 * 2. 第二条 observation 已经入队但还没被 worker 转成 record。
 * 3. CurrentSeq() 不能因为“已入队”提前前进，只能在 buildRecord 时递增。
 *
 * 示例：
 *
 *   publish(first) -> worker build seq=1 -> sink 阻塞
 *   publish(second) -> queued
 *        |
 *        v
 *   unblock 前 CurrentSeq()==1，unblock 后第二条变成 seq=2
 */
TEST(TestProtocolInteraction, PublisherCurrentSeqDoesNotAdvanceBeforeQueuedObservationIsBuilt)
{
    auto sink = std::make_shared<BlockingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    publisher.publish(MakeHttpObservation(1, 12, "/blocked-first"));
    ASSERT_TRUE(sink->WaitForRecordCount(1));

    publisher.publish(MakeHttpObservation(1, 12, "/queued-second"));
    EXPECT_EQ(publisher.CurrentSeq(), 1U);

    sink->Unblock();
    ASSERT_TRUE(sink->WaitForRecordCount(2));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].seq, 1U);
    EXPECT_EQ(records[0].request.meta["path"], "/blocked-first");
    EXPECT_EQ(records[1].seq, 2U);
    EXPECT_EQ(records[1].request.meta["path"], "/queued-second");
    EXPECT_EQ(publisher.CurrentSeq(), 2U);
}

/**
 * 测试思路：
 * 1. 同一个 Publisher 挂两个 sink，模拟同时投递给 Hub 和旁路记录器。
 * 2. 每个 sink 应收到同一份 record 语义，尤其 seq 必须一致。
 * 3. 这样才能保证多个实时消费端看到的是同一条交互记录，而不是各自重新编号。
 *
 * 示例：
 *
 *   Publisher -> sinkA
 *             -> sinkB
 *        |
 *        v
 *   sinkA[0].seq == sinkB[0].seq == 1
 */
TEST(TestProtocolInteraction, PublisherDeliversSameSeqToMultipleSinks)
{
    auto sink_a = std::make_shared<CollectingInteractionSink>();
    auto sink_b = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink_a, sink_b},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    publisher.publish(MakeHttpObservation(1, 12, "/multi-sink"));

    ASSERT_TRUE(sink_a->WaitForRecordCount(1));
    ASSERT_TRUE(sink_b->WaitForRecordCount(1));
    publisher.stop();

    const auto records_a = sink_a->Records();
    const auto records_b = sink_b->Records();
    ASSERT_EQ(records_a.size(), 1U);
    ASSERT_EQ(records_b.size(), 1U);
    EXPECT_EQ(records_a.front().seq, 1U);
    EXPECT_EQ(records_b.front().seq, 1U);
    EXPECT_EQ(records_a.front().request.meta["path"], "/multi-sink");
    EXPECT_EQ(records_b.front().request.meta["path"], "/multi-sink");
}

/**
 * 测试思路：
 * 1. 第一个 sink 故意抛异常，模拟 Hub 或旁路落库失败。
 * 2. Publisher 应捕获异常并继续投递给后续 sink。
 * 3. 后续 observation 也要继续处理，不能因为一次 sink 异常让 worker 停掉。
 *
 * 示例：
 *
 *   throwingSink.publish(seq=1) throws
 *        |
 *        +-- collectingSink 仍收到 seq=1
 *   publish(seq=2) 后 collectingSink 继续收到 seq=2
 */
TEST(TestProtocolInteraction, PublisherCatchesSinkExceptionAndContinues)
{
    auto throwing_sink = std::make_shared<ThrowingInteractionSink>();
    auto collecting_sink = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {throwing_sink, collecting_sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    publisher.publish(MakeHttpObservation(1, 12, "/throw-on-first"));
    publisher.publish(MakeHttpObservation(1, 12, "/still-works"));

    ASSERT_TRUE(collecting_sink->WaitForRecordCount(2));
    publisher.stop();

    const auto records = collecting_sink->Records();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].seq, 1U);
    EXPECT_EQ(records[0].request.meta["path"], "/throw-on-first");
    EXPECT_EQ(records[1].seq, 2U);
    EXPECT_EQ(records[1].request.meta["path"], "/still-works");
    EXPECT_EQ(throwing_sink->publishCount(), 2);
}

/**
 * 测试思路：
 * 1. Publisher 没有任何 sink 时也允许运行，运行态线程不应因此阻塞或崩溃。
 * 2. 发布两条 observation 后等待 CurrentSeq 前进到 2。
 * 3. 这固定“空 sinks 只记录 warning，worker 继续处理后续 observation”的边界。
 *
 * 示例：
 *
 *   Publisher(sinks=[])
 *        + publish("/empty-a")
 *        + publish("/empty-b")
 *        v
 *   CurrentSeq() 最终到 2
 */
TEST(TestProtocolInteraction, PublisherWithEmptySinksStillProcessesLaterObservations)
{
    ProtocolInteractionPublisher publisher(
        {},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    publisher.publish(MakeHttpObservation(1, 12, "/empty-a"));
    publisher.publish(MakeHttpObservation(1, 12, "/empty-b"));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while(publisher.CurrentSeq() < 2U && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    publisher.stop();

    EXPECT_EQ(publisher.CurrentSeq(), 2U);
}

/**
 * 测试思路：
 * 1. 发布 observation 后立即 stop，不显式等待 sink。
 * 2. stop() 会通知 worker 停止，并执行 best-effort drain。
 * 3. 队列中已经入队的 observation 应在 stop 返回前尽量被转换并投递。
 *
 * 示例：
 *
 *   publish("/drain-on-stop")
 *   stop()
 *        |
 *        v
 *   sink 收到 seq=1
 */
TEST(TestProtocolInteraction, PublisherStopDrainsQueuedObservationBestEffort)
{
    auto sink = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    publisher.publish(MakeHttpObservation(1, 12, "/drain-on-stop"));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().seq, 1U);
    EXPECT_EQ(records.front().request.meta["path"], "/drain-on-stop");
}

/**
 * 测试思路：
 * 1. body 期望 XML，实际内容是安全 UTF-8，但 XML 语法不合法。
 * 2. 这种错误应和 broken JSON 一样降级为 text 展示，不升级成 raw_packet。
 * 3. 错误原因进入 body.error_message，attachments 和 sidecar 都应为空。
 *
 * 示例：
 *
 *   expect=xml + "<root><id></root>"
 *        |
 *        v
 *   body.kind=text, body.error_message!=空, raw_packet 不出现
 */
TEST(TestProtocolInteraction, BrokenXmlBodyFallsBackToTextAndDoesNotNeedRawPacket)
{
    const auto data = Bytes("<root><id></root>");
    std::vector<BinarySidecar> sidecars;

    InteractionSide side;
    side.body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kXml, "application/xml"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);

    const nlohmann::json json = side;

    EXPECT_EQ(side.body.kind, InteractionPayloadKind::kText);
    EXPECT_EQ(side.body.expect_kind, InteractionPayloadKind::kXml);
    EXPECT_EQ(side.body.text, "<root><id></root>");
    EXPECT_FALSE(side.body.error_message.empty());
    EXPECT_TRUE(side.body.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
    EXPECT_FALSE(json.contains("raw_packet"));
}

/**
 * 测试思路：
 * 1. HTTP body 配置为 binary 时，Content-Type 决定前端展示大类。
 * 2. audio/video/archive/form-data/未知二进制都应走附件路径，避免原始 bytes 进入 JSON text。
 * 3. 每种类型都应生成一个 attachment，并在未截断时生成对应 sidecar bytes。
 *
 * 示例：
 *
 *   audio/mpeg              -> kind=audio
 *   video/mp4               -> kind=video
 *   application/zip         -> kind=archive
 *   multipart/form-data     -> kind=form_data
 *   application/x-custom    -> kind=binary
 */
TEST(TestProtocolInteraction, HttpBinaryMediaTypesBuildExpectedAttachmentKinds)
{
    struct Case
    {
        std::string media_type;
        InteractionPayloadKind expected_kind;
    };

    const std::vector<Case> cases = {
        {"audio/mpeg", InteractionPayloadKind::kAudio},
        {"video/mp4", InteractionPayloadKind::kVideo},
        {"application/zip", InteractionPayloadKind::kArchive},
        {"multipart/form-data; boundary=kit", InteractionPayloadKind::kMultiForm},
        {"application/x-custom-binary", InteractionPayloadKind::kBinary},
    };

    const std::vector<uint8_t> data{0x01, 0x02, 0x03, 0x04};
    for(const auto &c : cases)
    {
        SCOPED_TRACE(c.media_type);
        std::vector<BinarySidecar> sidecars;

        const auto body = InteractionBody::BuildFromBytes(
            data,
            Hint(ProtocolType::kHttp, ProtocolBodyType::kBinary, c.media_type),
            ProtocolSide::kResponse,
            Options(),
            sidecars);

        EXPECT_EQ(body.kind, c.expected_kind);
        EXPECT_EQ(body.expect_kind, InteractionPayloadKind::kBinary);
        ASSERT_EQ(body.attachments.size(), 1U);
        EXPECT_EQ(body.attachments.front().kind, c.expected_kind);
        EXPECT_TRUE(body.attachments.front().binary_available);
        ASSERT_EQ(sidecars.size(), 1U);
        ASSERT_NE(sidecars.front().bytes, nullptr);
        EXPECT_EQ(*sidecars.front().bytes, data);
    }
}

/**
 * 测试思路：
 * 1. attachment ref 是给前端匹配二进制帧的轻量元数据。
 * 2. expect_kind 和 media_type 属于 body 分类输入，不应重复出现在 attachment JSON。
 * 3. 该用例固定附件 JSON 字段边界，避免前端误依赖内部分类细节。
 *
 * 示例：
 *
 *   body.attachments[0]
 *        |
 *        v
 *   包含 attachment_id/kind/size/sha1，不包含 expect_kind/media_type
 */
TEST(TestProtocolInteraction, AttachmentJsonOmitsExpectKindAndMediaType)
{
    const std::vector<uint8_t> data{0x89, 'P', 'N', 'G'};
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kBinary, "image/png"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);

    const nlohmann::json json = body;
    ASSERT_TRUE(json.contains("attachments"));
    ASSERT_EQ(json["attachments"].size(), 1U);

    const auto &attachment_json = json["attachments"][0];
    EXPECT_TRUE(attachment_json.contains("attachment_id"));
    EXPECT_TRUE(attachment_json.contains("kind"));
    EXPECT_TRUE(attachment_json.contains("size"));
    EXPECT_TRUE(attachment_json.contains("sha1"));
    EXPECT_FALSE(attachment_json.contains("expect_kind"));
    EXPECT_FALSE(attachment_json.contains("media_type"));
}

/**
 * 测试思路：
 * 1. raw_packet 在 V1 只用于展示无法解析报文的 hex 前缀。
 * 2. raw_packet.attachments[] 固定为空，也不能额外产生 BinarySidecar。
 * 3. 这样前端不会把 parser error 的原始包误当作可下载附件。
 *
 * 示例：
 *
 *   raw bytes=[0xBA,0xD0]
 *        |
 *        v
 *   raw_packet.raw_hex="HBA D0", attachments=[], sidecars=[]
 */
TEST(TestProtocolInteraction, RawPacketAttachmentsStayEmptyAndDoNotCreateSidecar)
{
    const std::vector<uint8_t> data{0xBA, 0xD0};
    std::vector<BinarySidecar> sidecars;

    const auto raw_packet = InteractionRawPacket::BuildRawPacketFromBytes(
        data,
        ProtocolSide::kRequest,
        Options(),
        sidecars);
    const nlohmann::json json = raw_packet;

    EXPECT_EQ(raw_packet.raw_hex, "HBA D0");
    EXPECT_TRUE(raw_packet.attachments.empty());
    EXPECT_TRUE(sidecars.empty());
    ASSERT_TRUE(json.contains("attachments"));
    EXPECT_TRUE(json["attachments"].empty());
}

/**
 * 测试思路：
 * 1. 不只直接测 InteractionBody，还要从 Publisher observation 侧覆盖 HTTP 二进制 body。
 * 2. request 使用 image/png，response 使用 application/zip。
 * 3. Publisher 输出的 record 应带两个 sidecar，且 request/response body 各自有附件元数据。
 *
 * 示例：
 *
 *   Observation(request image bytes, response zip bytes)
 *        |
 *        v
 *   Record.request.body.kind=image
 *   Record.response.body.kind=archive
 *   Record.binary_sidecars.size()==2
 */
TEST(TestProtocolInteraction, PublisherBuildsHttpBinaryBodySidecarsFromObservation)
{
    auto sink = std::make_shared<CollectingInteractionSink>();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    ProtocolInteractionObservation obs;
    obs.scope = InteractionScope::kProtocol;
    obs.project_id = 1;
    obs.protocol_id = 12;
    obs.protocol_type = ProtocolType::kHttp;
    obs.time_ms = 1780000020000;
    obs.peer_addr = "127.0.0.1:53020";
    obs.result = InteractionResult::kMatched;
    obs.request.meta = {{"method", "POST"}, {"path", "/upload"}};
    obs.request.head_text = "POST /upload HTTP/1.1\r\nContent-Type: image/png\r\n\r\n";
    obs.request.body_bytes = std::vector<uint8_t>{0x89, 'P', 'N', 'G'};
    obs.request.expect_body_type = ProtocolBodyType::kBinary;
    obs.request.media_type = "image/png";
    obs.response.meta = {{"status_code", 200}};
    obs.response.head_text = "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\n\r\n";
    obs.response.body_bytes = std::vector<uint8_t>{'P', 'K', 0x03, 0x04};
    obs.response.expect_body_type = ProtocolBodyType::kBinary;
    obs.response.media_type = "application/zip";

    publisher.start();
    publisher.publish(std::move(obs));

    ASSERT_TRUE(sink->WaitForRecordCount(1));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.request.body.kind, InteractionPayloadKind::kImage);
    ASSERT_EQ(record.request.body.attachments.size(), 1U);
    EXPECT_EQ(record.request.body.attachments.front().flag, "request.body");
    EXPECT_EQ(record.response.body.kind, InteractionPayloadKind::kArchive);
    ASSERT_EQ(record.response.body.attachments.size(), 1U);
    EXPECT_EQ(record.response.body.attachments.front().flag, "response.body");
    ASSERT_EQ(record.binary_sidecars.size(), 2U);
    ASSERT_NE(record.binary_sidecars[0].bytes, nullptr);
    ASSERT_NE(record.binary_sidecars[1].bytes, nullptr);
    EXPECT_EQ(*record.binary_sidecars[0].bytes, (std::vector<uint8_t>{0x89, 'P', 'N', 'G'}));
    EXPECT_EQ(*record.binary_sidecars[1].bytes, (std::vector<uint8_t>{'P', 'K', 0x03, 0x04}));
}
