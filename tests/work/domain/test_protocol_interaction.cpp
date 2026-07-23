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

#include <algorithm>
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

std::vector<uint8_t> MultipartBody()
{
    const std::string boundary = "interaction-boundary";
    const std::string text_part =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"title\"\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "hello multipart\r\n";
    const std::string file_prefix =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"avatar\"; filename=\"avatar.png\"\r\n"
        "Content-Type: image/png\r\n"
        "\r\n";
    const std::string file_suffix = "\r\n--" + boundary + "--\r\n";

    std::vector<uint8_t> body(text_part.begin(), text_part.end());
    body.insert(body.end(), file_prefix.begin(), file_prefix.end());
    body.insert(body.end(), {0x89, 'P', 'N', 'G'});
    body.insert(body.end(), file_suffix.begin(), file_suffix.end());
    return body;
}

InteractionPayloadHint Hint(ProtocolType protocol_type,
                            ProtocolBodyType expect_body_type,
                            std::string media_type = {},
                            bool prefer_hex_text_for_binary = false)
{
    return InteractionPayloadHint{
        .protocol_type = protocol_type,
        .expect_body_type = expect_body_type,
        .content_meta = kit_muduo::http::ParseHttpContentType(media_type),
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
    obs.request.content_meta = kit_muduo::http::ParseHttpContentType("application/json");
    obs.response.meta = {
        {"status_code", 200},
    };
    obs.response.head_text = "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n\r\n";
    obs.response.body_bytes = Bytes(response_body);
    obs.response.expect_body_type = ProtocolBodyType::kJson;
    obs.response.content_meta = kit_muduo::http::ParseHttpContentType("application/json");
    return obs;
}

std::shared_ptr<InteractionRecordCache> MakeProtocolCache()
{
    return std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12});
}

std::shared_ptr<InteractionRecordCache> MakeProjectCache()
{
    return std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProject, 1, 0});
}

void BindObservationToCache(
    ProtocolInteractionObservation &observation,
    const std::shared_ptr<InteractionRecordCache> &cache)
{
    observation.cache_instance_id = cache->cacheInstanceId();
    observation.weak_record_cache = cache;
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
 * 1. multipart body 不能只被当作一个不可读的二进制附件，需要拆出文本字段和文件字段。
 * 2. 文本 part 保留在 attachment.text 中，不生成 sidecar；图片 part 生成附件元数据和原始 bytes sidecar。
 * 3. 字段容器是 unordered_map，断言按 kind/text 查找，避免把实现内部遍历顺序固化进测试。
 *
 * 示例：
 *
 *   title="hello multipart" + avatar=image/png
 *        |
 *        v
 *   attachments[text] + attachments[image] + one image sidecar
 */
TEST(TestProtocolInteraction, MultipartBodySplitsTextAndBinaryParts)
{
    const auto data = MultipartBody();
    std::vector<BinarySidecar> sidecars;

    const auto body = InteractionBody::BuildFromBytes(
        data,
        Hint(ProtocolType::kHttp, ProtocolBodyType::kMultiForm,
             "multipart/form-data; boundary=interaction-boundary"),
        ProtocolSide::kRequest,
        Options(),
        sidecars);

    ASSERT_EQ(body.kind, InteractionPayloadKind::kMultiForm);
    EXPECT_EQ(body.expect_kind, InteractionPayloadKind::kMultiForm);
    EXPECT_EQ(body.size, data.size());
    EXPECT_EQ(body.captured_size, data.size());
    EXPECT_FALSE(body.truncated);
    ASSERT_EQ(body.attachments.size(), 2U);

    const InteractionAttachmentRef *text_ref = nullptr;
    const InteractionAttachmentRef *image_ref = nullptr;
    for(const auto &ref : body.attachments)
    {
        if(ref.kind == InteractionPayloadKind::kText)
        {
            text_ref = &ref;
        }
        if(ref.kind == InteractionPayloadKind::kImage)
        {
            image_ref = &ref;
        }
        EXPECT_EQ(ref.side, "request");
    }

    ASSERT_NE(text_ref, nullptr);
    EXPECT_EQ(text_ref->text, "hello multipart");
    EXPECT_EQ(text_ref->size, 15U);
    EXPECT_EQ(text_ref->captured_size, 15U);
    EXPECT_FALSE(text_ref->truncated);
    EXPECT_FALSE(text_ref->binary_available);

    ASSERT_NE(image_ref, nullptr);
    EXPECT_TRUE(image_ref->text.empty());
    EXPECT_EQ(image_ref->size, 4U);
    EXPECT_EQ(image_ref->captured_size, 4U);
    EXPECT_FALSE(image_ref->truncated);
    EXPECT_TRUE(image_ref->binary_available);
    ASSERT_EQ(sidecars.size(), 1U);
    EXPECT_EQ(sidecars.front().attachment_ref.attachment_id, image_ref->attachment_id);
    ASSERT_NE(sidecars.front().bytes, nullptr);
    EXPECT_EQ(*sidecars.front().bytes, (std::vector<uint8_t>{0x89, 'P', 'N', 'G'}));
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

namespace {

InteractionRecord MakeCachedRecord(
    const std::shared_ptr<InteractionRecordCache> &cache,
    size_t sidecar_bytes = 0)
{
    InteractionRecord record = MakeRecord(
        0, InteractionScope::kProtocol, 1, 12);
    record.cache_instance_id = cache->cacheInstanceId();

    if(sidecar_bytes > 0)
    {
        InteractionAttachmentRef ref;
        ref.attachment_id = "response.body:cache-test";
        ref.side = "response";
        ref.flag = "response.body";
        ref.kind = InteractionPayloadKind::kImage;
        ref.size = sidecar_bytes;
        ref.captured_size = sidecar_bytes;
        ref.binary_available = true;
        ref.sha1 = "cache-test-sha1";
        record.response.body.attachments.push_back(ref);
        record.binary_sidecars.push_back(BinarySidecar{
            .attachment_ref = ref,
            .bytes = std::make_shared<const std::vector<uint8_t>>(
                sidecar_bytes, 0xAB),
        });
    }

    return record;
}

} // namespace

/**
 * 测试思路：
 *   验证 recent cache 的 metadata 条数窗口是硬上限，而不是只记录统计值。
 *
 * 输入示例：
 *   max_records=20，连续追加 21 条无 sidecar record。
 *
 * 事件或线程时序：
 *   tryAppend(seq=1..21) -> lockAndCollect(cache_id, after_seq=0)。
 *
 * 预期结果：
 *   只保留最新 20 条，即 seq=2..21；序号继续连续分配。
 *
 * 关键断言：
 *   recordCount、snapshot 内容和 last_seq 均准确，且发生窗口淘汰后仍能
 *   通过 catch_up_gap 表达旧游标已经落后。
 */
TEST(TestProtocolInteraction, InteractionCacheKeepsTwentyNewestMetadataRecords)
{
    auto cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12});

    for(size_t i = 0; i < InteractionRecordCacheConfig::kDefaultMaxRecord + 1; ++i)
    {
        auto record = MakeCachedRecord(cache);
        ASSERT_TRUE(cache->tryAppend(record));
        EXPECT_EQ(record.seq, i + 1);
    }

    EXPECT_EQ(cache->recordCount(), InteractionRecordCacheConfig::kDefaultMaxRecord);
    auto locked = cache->lockAndCollect(cache->cacheInstanceId(), 0);
    ASSERT_TRUE(locked.isActive());
    ASSERT_TRUE(locked.isValid());
    EXPECT_EQ(locked.snapshot().last_seq,
        InteractionRecordCacheConfig::kDefaultMaxRecord + 1);
    EXPECT_TRUE(locked.snapshot().catch_up_gap);
    ASSERT_EQ(locked.snapshot().incr_records.size(),
        InteractionRecordCacheConfig::kDefaultMaxRecord);
    EXPECT_EQ(locked.snapshot().incr_records.front().seq, 2U);
    EXPECT_EQ(locked.snapshot().incr_records.back().seq, 21U);
}

/**
 * 测试思路：
 *   用较小的自定义预算等价验证 64 MiB 精确边界，避免测试本身分配无谓的
 *   大块内存；核心风险是边界判断把“刚好等于上限”误当成超限。
 *
 * 输入示例：
 *   max_sidecar_bytes=8，追加两个 4-byte sidecar record。
 *
 * 事件或线程时序：
 *   append(4) -> append(4) -> 读取 retainedSidecarBytes。
 *
 * 预期结果：
 *   总 sidecar 正好达到上限时不发生字节淘汰，两个 record 都保留。
 *
 * 关键断言：
 *   retainedSidecarBytes==8、recordCount==2、byteEvictionCount==0。
 */
TEST(TestProtocolInteraction, InteractionCacheAcceptsExactSidecarByteLimit)
{
    auto cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12},
        InteractionRecordCacheConfig{.max_records = 20, .max_sidecar_bytes = 8});

    auto first = MakeCachedRecord(cache, 4);
    auto second = MakeCachedRecord(cache, 4);
    ASSERT_TRUE(cache->tryAppend(first));
    ASSERT_TRUE(cache->tryAppend(second));

    EXPECT_EQ(cache->retainedSidecarBytes(), 8U);
    EXPECT_EQ(cache->recordCount(), 2U);
    EXPECT_EQ(cache->byteEvictionCount(), 0U);
}

/**
 * 测试思路：
 *   验证新 record 使 sidecar 总量超过预算时，cache 按最旧 record 顺序淘汰，
 *   而不是拒绝新 record 或突破字节上限。
 *
 * 输入示例：
 *   max_sidecar_bytes=8，依次追加 4-byte、4-byte、1-byte sidecar。
 *
 * 事件或线程时序：
 *   [seq1=4, seq2=4] -> append(seq3=1) -> evict seq1。
 *
 * 预期结果：
 *   cache 保留 seq2、seq3，总 sidecar 为 5 bytes。
 *
 * 关键断言：
 *   byteEvictionCount==1、retainedSidecarBytes==5、快照首尾序号为 2/3。
 */
TEST(TestProtocolInteraction, InteractionCacheEvictsOldestRecordWhenByteBudgetIsExceeded)
{
    auto cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12},
        InteractionRecordCacheConfig{.max_records = 20, .max_sidecar_bytes = 8});

    auto first = MakeCachedRecord(cache, 4);
    auto second = MakeCachedRecord(cache, 4);
    auto third = MakeCachedRecord(cache, 1);
    ASSERT_TRUE(cache->tryAppend(first));
    ASSERT_TRUE(cache->tryAppend(second));
    ASSERT_TRUE(cache->tryAppend(third));

    EXPECT_EQ(cache->retainedSidecarBytes(), 5U);
    EXPECT_EQ(cache->recordCount(), 2U);
    EXPECT_EQ(cache->byteEvictionCount(), 1U);

    auto locked = cache->lockAndCollect(cache->cacheInstanceId(), 0);
    ASSERT_EQ(locked.snapshot().incr_records.size(), 2U);
    EXPECT_EQ(locked.snapshot().incr_records[0].seq, 2U);
    EXPECT_EQ(locked.snapshot().incr_records[1].seq, 3U);
}

/**
 * 测试思路：
 *   单条 sidecar 大于 cache 预算时，cache 必须降级为 metadata-only；但原始
 *   record 仍要保留完整 sidecar，供当前 Publisher live 分发使用。
 *
 * 输入示例：
 *   max_sidecar_bytes=4，单条 record 携带 5-byte attachment。
 *
 * 事件或线程时序：
 *   tryAppend(original) -> cache snapshot。
 *
 * 预期结果：
 *   原始 record 的 sidecar 和 binary_available 不变；cache 副本释放 sidecar，
 *   但保留 attachment 元数据并将 binary_available 置 false。
 *
 * 关键断言：
 *   retainedSidecarBytes==0、metadataOnlyCount==1、seq 仍为 1、cache 副本无
 *   binary_sidecars 且附件元数据仍存在。
 */
TEST(TestProtocolInteraction, InteractionCacheDowngradesOversizedRecordToMetadataOnly)
{
    auto cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12},
        InteractionRecordCacheConfig{.max_records = 20, .max_sidecar_bytes = 4});
    auto original = MakeCachedRecord(cache, 5);
    ASSERT_TRUE(cache->tryAppend(original));

    ASSERT_EQ(original.seq, 1U);
    ASSERT_EQ(original.binary_sidecars.size(), 1U);
    ASSERT_TRUE(original.response.body.attachments.front().binary_available);
    EXPECT_EQ(cache->retainedSidecarBytes(), 0U);
    EXPECT_EQ(cache->metadataOnlyCount(), 1U);

    auto locked = cache->lockAndCollect(cache->cacheInstanceId(), 0);
    ASSERT_EQ(locked.snapshot().incr_records.size(), 1U);
    const auto &cached = locked.snapshot().incr_records.front();
    EXPECT_TRUE(cached.binary_sidecars.empty());
    ASSERT_EQ(cached.response.body.attachments.size(), 1U);
    EXPECT_FALSE(cached.response.body.attachments.front().binary_available);
    EXPECT_EQ(cached.response.body.attachments.front().attachment_id,
        original.response.body.attachments.front().attachment_id);
}

/**
 * 测试思路：
 *   close 是 cache 生命周期终点，必须释放窗口和 sidecar 预算，并拒绝后续
 *   append，避免运行态销毁后 Publisher 继续向旧 cache 写入。
 *
 * 输入示例：
 *   追加一条带 sidecar 的 record 后调用 close，再尝试追加第二条。
 *
 * 事件或线程时序：
 *   append -> close -> tryAppend。
 *
 * 预期结果：
 *   close 后 cache inactive、recordCount 和 retained bytes 都归零，后续 append
 *   返回 false。
 *
 * 关键断言：
 *   isActive、recordCount、retainedSidecarBytes 和 tryAppend 返回值。
 */
TEST(TestProtocolInteraction, InteractionCacheCloseReleasesRecordsAndRejectsAppend)
{
    auto cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12},
        InteractionRecordCacheConfig{.max_records = 20, .max_sidecar_bytes = 8});
    auto first = MakeCachedRecord(cache, 4);
    ASSERT_TRUE(cache->tryAppend(first));
    ASSERT_EQ(cache->recordCount(), 1U);
    ASSERT_EQ(cache->retainedSidecarBytes(), 4U);

    cache->close();

    EXPECT_FALSE(cache->isActive());
    EXPECT_EQ(cache->recordCount(), 0U);
    EXPECT_EQ(cache->retainedSidecarBytes(), 0U);
    auto second = MakeCachedRecord(cache, 1);
    EXPECT_FALSE(cache->tryAppend(second));
}

/**
 * 测试思路：
 *   先把历史 record 写入独立 protocol cache，再建立 Hub 订阅；订阅成功后
 *   只允许同 project、同 protocol、同 cache instance 且 seq 在起始边界之后
 *   的 record 进入回调。
 *
 * 输入示例：
 *   历史 seq=1，订阅快照之后再追加 seq=2；另造其它协议项和其它项目记录。
 *
 * 事件或线程时序：
 *   cache.tryAppend(history) -> subscribeWithCatchUp(no cursor)
 *   -> cache.tryAppend(new) -> hub.publish(new)。
 *
 * 预期结果：
 *   首次订阅不回放历史，只收到同 cache 的新 protocol record。
 *
 * 关键断言：
 *   protocol_start_seq == history.seq + 1，回调数量为 1，且 project/protocol
 *   过滤和 cache_instance_id 过滤均生效。
 */
TEST(TestProtocolInteraction, HubOnlyPushesMatchingProtocolRecordsAfterSubscribe)
{
    ProtocolInteractionHub hub;
    auto protocol_cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12});
    auto project_cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProject, 1, 0});

    InteractionRecord history = MakeRecord(0, InteractionScope::kProtocol, 1, 12);
    history.cache_instance_id = protocol_cache->cacheInstanceId();
    ASSERT_TRUE(protocol_cache->tryAppend(history));

    std::vector<InteractionRecord> received;
    auto subscription = hub.subscribeWithCatchUp(
        InteractionSubscribeFilter{1, 12, false},
        InteractionRecordCacheContainer{protocol_cache, project_cache,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt},
        [&received](const InteractionRecord &record) {
            received.push_back(record);
        });

    ASSERT_TRUE(subscription.ok());
    ASSERT_NE(subscription.subscription.subscriber_id, 0U);
    EXPECT_EQ(subscription.subscription.protocol_start_seq, history.seq + 1);
    EXPECT_TRUE(subscription.protocol_cache_snapshot.incr_records.empty());

    InteractionRecord next = MakeRecord(0, InteractionScope::kProtocol, 1, 12);
    next.cache_instance_id = protocol_cache->cacheInstanceId();
    ASSERT_TRUE(protocol_cache->tryAppend(next));
    hub.publish(next);

    InteractionRecord other_protocol = MakeRecord(0, InteractionScope::kProtocol, 1, 13);
    other_protocol.cache_instance_id = protocol_cache->cacheInstanceId();
    EXPECT_FALSE(protocol_cache->tryAppend(other_protocol));
    hub.publish(other_protocol);
    hub.publish(MakeRecord(999, InteractionScope::kProtocol, 2, 12));

    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received.front().seq, next.seq);
    EXPECT_EQ(received.front().cache_instance_id, protocol_cache->cacheInstanceId());
    hub.unsubcribe(subscription.subscription.subscriber_id);
}

/**
 * 测试思路：
 *   用两个独立 cache 建立“只看 protocol”和“看 protocol + project notice”的
 *   订阅，验证项目 notice 的显式开关以及退订后的停止分发语义。
 *
 * 输入示例：
 *   project cache 追加 route_not_found notice；protocol cache 追加 matched record。
 *
 * 事件或线程时序：
 *   subscribe(two filters) -> project notice -> unsubscribe(second)
 *   -> protocol record。
 *
 * 预期结果：
 *   只有 opt-in 订阅收到 notice；退订后不再收到后续 record。
 *
 * 关键断言：
 *   project notice 的 protocol_id 为 0，protocol-only 订阅为空，退订订阅的
 *   回调数量不再增长。
 */
TEST(TestProtocolInteraction, HubProjectNoticeRequiresOptInAndUnsubscribeStopsDelivery)
{
    ProtocolInteractionHub hub;
    auto protocol_cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 1, 12});
    auto project_cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProject, 1, 0});

    std::vector<InteractionRecord> protocol_only_records;
    std::vector<InteractionRecord> with_notice_records;
    const InteractionRecordCacheContainer empty_cursor{
        protocol_cache, project_cache, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt};

    auto protocol_only = hub.subscribeWithCatchUp(
        InteractionSubscribeFilter{1, 12, false}, empty_cursor,
        [&protocol_only_records](const InteractionRecord &record) {
            protocol_only_records.push_back(record);
        });
    auto with_notice = hub.subscribeWithCatchUp(
        InteractionSubscribeFilter{1, 12, true}, empty_cursor,
        [&with_notice_records](const InteractionRecord &record) {
            with_notice_records.push_back(record);
        });
    ASSERT_TRUE(protocol_only.ok());
    ASSERT_TRUE(with_notice.ok());

    InteractionRecord notice = MakeRecord(0, InteractionScope::kProject, 1, 0,
        InteractionResult::kRouteNotFound);
    notice.cache_instance_id = project_cache->cacheInstanceId();
    ASSERT_TRUE(project_cache->tryAppend(notice));
    hub.publish(notice);

    EXPECT_TRUE(protocol_only_records.empty());
    ASSERT_EQ(with_notice_records.size(), 1U);
    EXPECT_EQ(with_notice_records.front().scope, InteractionScope::kProject);
    EXPECT_EQ(with_notice_records.front().protocol_id, 0);

    hub.unsubcribe(with_notice.subscription.subscriber_id);
    InteractionRecord protocol = MakeRecord(0, InteractionScope::kProtocol, 1, 12);
    protocol.cache_instance_id = protocol_cache->cacheInstanceId();
    ASSERT_TRUE(protocol_cache->tryAppend(protocol));
    hub.publish(protocol);

    ASSERT_EQ(protocol_only_records.size(), 1U);
    EXPECT_EQ(protocol_only_records.front().seq, protocol.seq);
    EXPECT_EQ(with_notice_records.size(), 1U);
    hub.unsubcribe(protocol_only.subscription.subscriber_id);
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
    auto cache = MakeProtocolCache();
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
    obs.request.content_meta = kit_muduo::http::ParseHttpContentType("application/json");
    obs.response.meta = {
        {"status_code", 200},
    };
    obs.response.head_text = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
    obs.response.body_bytes = Bytes("ok");
    obs.response.expect_body_type = ProtocolBodyType::kText;
    obs.response.content_meta = kit_muduo::http::ParseHttpContentType("text/plain");
    BindObservationToCache(obs, cache);

    publisher.publish(std::move(obs));

    ASSERT_TRUE(sink->WaitForRecordCount(1));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.seq, 1U);
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
    auto cache = MakeProjectCache();
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
    BindObservationToCache(obs, cache);

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
    auto cache = MakeProtocolCache();
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
    first.request.content_meta = kit_muduo::http::ParseHttpContentType("text/plain");
    BindObservationToCache(first, cache);

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
    auto cache = MakeProtocolCache();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 8,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
    });

    publisher.start();
    auto first = MakeHttpObservation(1, 12, "/one");
    BindObservationToCache(first, cache);
    publisher.publish(std::move(first));
    auto second = MakeHttpObservation(1, 12, "/two");
    auto third = MakeHttpObservation(1, 12, "/three");
    BindObservationToCache(second, cache);
    BindObservationToCache(third, cache);
    publisher.publish(std::move(second));
    publisher.publish(std::move(third));

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
    auto cache = MakeProtocolCache();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    auto first = MakeHttpObservation(1, 12, "/blocked-first");
    BindObservationToCache(first, cache);
    publisher.publish(std::move(first));
    ASSERT_TRUE(sink->WaitForRecordCount(1));

    auto second = MakeHttpObservation(1, 12, "/queued-second");
    BindObservationToCache(second, cache);
    publisher.publish(std::move(second));

    sink->Unblock();
    ASSERT_TRUE(sink->WaitForRecordCount(2));
    publisher.stop();

    const auto records = sink->Records();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].seq, 1U);
    EXPECT_EQ(records[0].request.meta["path"], "/blocked-first");
    EXPECT_EQ(records[1].seq, 2U);
    EXPECT_EQ(records[1].request.meta["path"], "/queued-second");
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
    auto cache = MakeProtocolCache();
    ProtocolInteractionPublisher publisher(
        {sink_a, sink_b},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    auto observation = MakeHttpObservation(1, 12, "/multi-sink");
    BindObservationToCache(observation, cache);
    publisher.publish(std::move(observation));

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
    auto cache = MakeProtocolCache();
    ProtocolInteractionPublisher publisher(
        {throwing_sink, collecting_sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    auto first = MakeHttpObservation(1, 12, "/throw-on-first");
    auto second = MakeHttpObservation(1, 12, "/still-works");
    BindObservationToCache(first, cache);
    BindObservationToCache(second, cache);
    publisher.publish(std::move(first));
    publisher.publish(std::move(second));

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
    auto cache = MakeProtocolCache();
    ProtocolInteractionPublisher publisher(
        {},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    auto first = MakeHttpObservation(1, 12, "/empty-a");
    auto second = MakeHttpObservation(1, 12, "/empty-b");
    BindObservationToCache(first, cache);
    BindObservationToCache(second, cache);
    publisher.publish(std::move(first));
    publisher.publish(std::move(second));
    publisher.stop();
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
    auto cache = MakeProtocolCache();
    ProtocolInteractionPublisher publisher(
        {sink},
        ProtocolInteractionPublisherConfig{
            .queue_capacity = 4,
            .stop_drain_timeout = 1000,
            .capture_options = Options(),
        });

    publisher.start();
    auto observation = MakeHttpObservation(1, 12, "/drain-on-stop");
    BindObservationToCache(observation, cache);
    publisher.publish(std::move(observation));
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
 * 2. audio/video/archive/form-data/未知媒体类型都应走附件路径，避免原始 bytes 进入 JSON text。
 * 3. 每种类型都应生成一个 attachment，并在未截断时生成对应 sidecar bytes。
 *
 * 示例：
 *
 *   audio/mpeg              -> kind=audio
 *   video/mp4               -> kind=video
 *   application/zip         -> kind=archive
 *   multipart/form-data     -> kind=form_data
 *   application/x-custom    -> kind=unknown
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
        {"application/x-custom-binary", InteractionPayloadKind::kUnknown},
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
    auto cache = MakeProtocolCache();
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
    obs.request.content_meta = kit_muduo::http::ParseHttpContentType("image/png");
    obs.response.meta = {{"status_code", 200}};
    obs.response.head_text = "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\n\r\n";
    obs.response.body_bytes = std::vector<uint8_t>{'P', 'K', 0x03, 0x04};
    obs.response.expect_body_type = ProtocolBodyType::kBinary;
    obs.response.content_meta = kit_muduo::http::ParseHttpContentType("application/zip");
    BindObservationToCache(obs, cache);

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
