/**
 * @file test_web_protocol_interaction.cpp
 * @brief 协议项实时交互详情 WebSocket handler 测试
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/event_loop_thread.h"
#include "base/time_stamp.h"
#include "domain/protocol.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/type.h"
#include "net/buffer.h"
#include "net/event_loop.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/inet_address.h"
#include "net/tcp_connection.h"
#include "net/websocket/websocket_context.h"
#include "net/websocket/websocket_frame.h"
#include "net/websocket/websocket_server.h"
#include "net/websocket/websocket_session.h"
#include "net/websocket/websocket_util.h"
#include "runtime/mock/runtime_controller_mock.h"
#include "service/mock/svc_protocol_mock.h"
#define private public
#include "web/web_protocol_interaction.h"
#undef private

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_muduo::ws;
using namespace testing;

namespace {

constexpr const char *kClientKey = "dGhlIHNhbXBsZSBub25jZQ==";

struct FdGuard
{
    explicit FdGuard(int32_t input_fd = -1)
        : fd(input_fd)
    {
    }

    ~FdGuard()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
    }

    int32_t release()
    {
        int32_t out = fd;
        fd = -1;
        return out;
    }

    int32_t fd;
};

void ThrowSocketError(const std::string &operation)
{
    throw std::runtime_error(operation + " failed: " + std::strerror(errno));
}

void MakeTcpConnectionPair(FdGuard *server,
    FdGuard *client,
    InetAddress *server_peer_addr,
    InetAddress *server_local_addr)
{
    FdGuard listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if(listener.fd < 0)
    {
        ThrowSocketError("create loopback listener socket");
    }

    int32_t on = 1;
    if(::setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        ThrowSocketError("setsockopt SO_REUSEADDR");
    }

    sockaddr_in listen_addr;
    std::memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = ::htons(0);

    if(::bind(listener.fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)) < 0)
    {
        ThrowSocketError("bind loopback listener");
    }

    if(::listen(listener.fd, 1) < 0)
    {
        ThrowSocketError("listen loopback listener");
    }

    socklen_t listen_addr_len = sizeof(listen_addr);
    if(::getsockname(listener.fd, reinterpret_cast<sockaddr*>(&listen_addr), &listen_addr_len) < 0)
    {
        ThrowSocketError("getsockname loopback listener");
    }

    client->fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(client->fd < 0)
    {
        ThrowSocketError("create loopback client socket");
    }

    if(::connect(client->fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)) < 0)
    {
        ThrowSocketError("connect loopback client");
    }

    sockaddr_in peer_addr;
    std::memset(&peer_addr, 0, sizeof(peer_addr));
    socklen_t peer_addr_len = sizeof(peer_addr);
    server->fd = ::accept4(listener.fd,
        reinterpret_cast<sockaddr*>(&peer_addr),
        &peer_addr_len,
        SOCK_NONBLOCK | SOCK_CLOEXEC);
    if(server->fd < 0)
    {
        ThrowSocketError("accept loopback server socket");
    }

    *server_peer_addr = InetAddress(peer_addr);
    *server_local_addr = InetAddress::GetLocalAddr(server->fd);
}

std::vector<uint8_t> ToBytes(const std::string &text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

void AppendUint16BE(std::vector<uint8_t> *out, uint16_t value)
{
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendUint64BE(std::vector<uint8_t> *out, uint64_t value)
{
    for(int i = 7; i >= 0; --i)
    {
        out->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

std::vector<uint8_t> BuildClientFrame(WebSocketOpcode opcode, const std::vector<uint8_t> &payload)
{
    const std::vector<uint8_t> mask{0x31, 0x32, 0x33, 0x34};
    std::vector<uint8_t> out;
    out.push_back(0x80 | (static_cast<uint8_t>(opcode) & 0x0F));

    if(payload.size() <= 125)
    {
        out.push_back(0x80 | static_cast<uint8_t>(payload.size()));
    }
    else if(payload.size() <= 0xFFFF)
    {
        out.push_back(0x80 | 126);
        AppendUint16BE(&out, static_cast<uint16_t>(payload.size()));
    }
    else
    {
        out.push_back(0x80 | 127);
        AppendUint64BE(&out, static_cast<uint64_t>(payload.size()));
    }

    out.insert(out.end(), mask.begin(), mask.end());
    for(size_t i = 0; i < payload.size(); ++i)
    {
        out.push_back(payload[i] ^ mask[i % mask.size()]);
    }
    return out;
}

std::vector<uint8_t> ReadAvailable(int32_t fd)
{
    std::vector<uint8_t> out;
    while(true)
    {
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ready = ::poll(&pfd, 1, 100);
        if(ready <= 0)
        {
            break;
        }

        uint8_t buf[4096];
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        if(n <= 0)
        {
            break;
        }
        out.insert(out.end(), buf, buf + n);
    }
    return out;
}

std::vector<WebSocketFrame> DecodeServerFrames(const std::vector<uint8_t> &bytes)
{
    std::vector<WebSocketFrame> out;
    size_t offset = 0;
    while(offset + 2 <= bytes.size())
    {
        uint8_t byte0 = bytes[offset++];
        uint8_t byte1 = bytes[offset++];
        auto opcode = static_cast<WebSocketOpcode>(byte0 & 0x0F);
        bool masked = (byte1 & 0x80) != 0;
        uint64_t payload_len = byte1 & 0x7F;
        if(payload_len == 126)
        {
            if(offset + 2 > bytes.size())
            {
                break;
            }
            payload_len = (static_cast<uint64_t>(bytes[offset]) << 8) | bytes[offset + 1];
            offset += 2;
        }
        else if(payload_len == 127)
        {
            if(offset + 8 > bytes.size())
            {
                break;
            }
            payload_len = 0;
            for(int i = 0; i < 8; ++i)
            {
                payload_len = (payload_len << 8) | bytes[offset + i];
            }
            offset += 8;
        }
        std::array<uint8_t, 4> mask{};
        if(masked)
        {
            if(offset + mask.size() > bytes.size())
            {
                break;
            }
            std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + mask.size()),
                mask.begin());
            offset += mask.size();
        }
        if(offset + payload_len > bytes.size())
        {
            break;
        }
        WebSocketFrame frame;
        frame.fin = (byte0 & 0x80) != 0;
        frame.opcode = opcode;
        frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + payload_len));
        if(masked)
        {
            for(size_t i = 0; i < frame.payload.size(); ++i)
            {
                frame.payload[i] ^= mask[i % mask.size()];
            }
        }
        out.push_back(std::move(frame));
        offset += static_cast<size_t>(payload_len);
    }
    return out;
}

std::vector<std::string> DecodeServerTextFrames(const std::vector<uint8_t> &bytes)
{
    std::vector<std::string> out;
    for(const auto &frame : DecodeServerFrames(bytes))
    {
        if(frame.opcode == WebSocketOpcode::kText)
        {
            out.emplace_back(frame.payload.begin(), frame.payload.end());
        }
    }
    return out;
}

uint32_t ReadUint32BE(const std::vector<uint8_t> &bytes, size_t offset)
{
    return (static_cast<uint32_t>(bytes[offset]) << 24)
        | (static_cast<uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<uint32_t>(bytes[offset + 3]);
}

uint16_t ReadUint16BE(const std::vector<uint8_t> &bytes, size_t offset)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8)
        | static_cast<uint16_t>(bytes[offset + 1]));
}

nlohmann::json SingleJsonTextFrame(int32_t fd)
{
    auto texts = DecodeServerTextFrames(ReadAvailable(fd));
    EXPECT_EQ(texts.size(), 1U);
    if(texts.empty())
    {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(texts.front());
}

void AppendToBuffer(Buffer *buf, const std::vector<uint8_t> &bytes)
{
    buf->append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void RunInLoopSync(EventLoop *loop, std::function<void()> fn)
{
    if(loop->isInLoopThread())
    {
        fn();
        return;
    }

    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    loop->runInLoop([done, fn = std::move(fn)]() mutable {
        fn();
        done->set_value();
    });
    future.get();
}

HttpContextPtr MakeUpgradeContext(int64_t protocol_id)
{
    auto ctx = std::make_shared<HttpContext>();
    auto req = ctx->request();
    req->setMethod(HttpRequest::Method::kGet);
    req->setPath("/ws/protocol-interactions/live");
    req->setVersion(Version::kHttp11);
    req->addQureyParam("protocol_id", std::to_string(protocol_id));
    req->addHeader("Host", "localhost");
    req->addHeader("Connection", "Upgrade");
    req->addHeader("Upgrade", "websocket");
    req->addHeader("Sec-WebSocket-Version", "13");
    req->addHeader("Sec-WebSocket-Key", kClientKey);
    return ctx;
}

ProtocolAccessInfo MakeAccessInfo(int64_t project_id, int64_t protocol_id)
{
    return ProtocolAccessInfo{
        protocol_id,
        project_id,
        "HTTP|GET|/live",
        ProtocolType::kHttp,
        ProtocolStatus::kValid,
        ProtocolConfigState::kOn,
        1,
        ProjectRuntimeState::kRunning,
        ProjectStatus::kValid,
    };
}

ProtocolInteractionRecord MakeRecord(uint64_t seq,
                                     InteractionScope scope,
                                     int64_t project_id,
                                     int64_t protocol_id,
                                     InteractionResult result = InteractionResult::kMatched)
{
    ProtocolInteractionRecord record;
    record.seq = seq;
    record.scope = scope;
    record.project_id = project_id;
    record.protocol_id = protocol_id;
    record.protocol_type = ProtocolType::kHttp;
    record.time_ms = 1780000000000;
    record.peer_addr = "127.0.0.1:53001";
    record.result = result;
    return record;
}

struct InteractionWsFixture
{
    EventLoopThread loop_thread;
    EventLoop *loop{nullptr};
    FdGuard peer;
    TcpConnectionPtr conn;
    WebSocketServer server;
    std::shared_ptr<NiceMock<MockProtocolSvc>> protocol_svc;
    std::shared_ptr<NiceMock<MockRuntimeController>> runtime;
    std::shared_ptr<ProtocolInteractionHub> hub;
    ProtocolInteractionHandler handler;
    WebSocketSessionPtr session;

    explicit InteractionWsFixture(std::shared_ptr<ProtocolInteractionHub> shared_hub = nullptr)
        : loop_thread(nullptr, "interaction-ws-test-loop")
        , protocol_svc(std::make_shared<NiceMock<MockProtocolSvc>>())
        , runtime(std::make_shared<NiceMock<MockRuntimeController>>())
        , hub(shared_hub ? std::move(shared_hub) : std::make_shared<ProtocolInteractionHub>())
        , handler(protocol_svc, runtime, hub)
    {
        loop = loop_thread.startLoop();
        if(loop == nullptr)
        {
            throw std::runtime_error("start interaction websocket event loop failed");
        }

        FdGuard server_fd;
        InetAddress server_peer_addr;
        InetAddress server_local_addr;
        MakeTcpConnectionPair(&server_fd, &peer, &server_peer_addr, &server_local_addr);
        conn = std::make_shared<TcpConnection>(loop,
            "interaction-ws-test",
            server_fd.release(),
            server_peer_addr,
            server_local_addr);
        conn->setConnectionCallback([](const TcpConnectionPtr&) {});
        auto conn_copy = conn;
        RunInLoopSync(loop, [conn_copy]() {
            conn_copy->connectEstablished();
        });
    }

    ~InteractionWsFixture()
    {
        if(conn)
        {
            auto conn_copy = conn;
            conn.reset();
            RunInLoopSync(loop, [conn_copy]() {
                conn_copy->connectDestroyed();
            });
        }
    }

    void WaitForLoop()
    {
        RunInLoopSync(loop, []() {});
    }

    void Publish(ProtocolInteractionRecord record)
    {
        hub->publish(std::move(record));
        WaitForLoop();
    }

    void Upgrade(int64_t project_id, int64_t protocol_id)
    {
        EXPECT_CALL(*protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(DoAll(SetArgReferee<2>(MakeAccessInfo(project_id, protocol_id)), Return(true)));

        auto ctx = MakeUpgradeContext(protocol_id);
        server.handleUpgrade(conn, ctx, [this](WebSocketSessionPtr session, HttpContextPtr prepare_ctx) {
            this->session = session;
            return handler.onPrepare(std::move(session), std::move(prepare_ctx));
        });
        ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k101SwitchingProtocols);
        ASSERT_NE(session, nullptr);
        ASSERT_TRUE(session->isOpen());
        server.onOpen(conn);
        WaitForLoop();
    }

    void SendClientJson(const nlohmann::json &msg)
    {
        Buffer buf;
        AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kText, ToBytes(msg.dump())));
        server.drainRemainingWebSocketBytes(conn, &buf, TimeStamp::Now());
        WaitForLoop();
        EXPECT_EQ(buf.readableBytes(), 0U);
    }

    void SendClientFrame(WebSocketOpcode opcode, const std::vector<uint8_t> &payload)
    {
        Buffer buf;
        AppendToBuffer(&buf, BuildClientFrame(opcode, payload));
        server.drainRemainingWebSocketBytes(conn, &buf, TimeStamp::Now());
        WaitForLoop();
    }

    HttpContextPtr UpgradeDeniedByAccessFailure(int64_t protocol_id)
    {
        EXPECT_CALL(*protocol_svc, GetAccessInfo(_, protocol_id, _))
            .WillOnce(Return(false));

        auto ctx = MakeUpgradeContext(protocol_id);
        server.handleUpgrade(conn, ctx, [this](WebSocketSessionPtr session, HttpContextPtr prepare_ctx) {
            this->session = session;
            return handler.onPrepare(std::move(session), std::move(prepare_ctx));
        });
        WaitForLoop();
        return ctx;
    }
};

} // namespace

/**
 * 测试思路：
 * 1. WebSocket upgrade 业务准备阶段只信任 protocol_id，真实 project_id 由 ProtocolSvc::GetAccessInfo 反查。
 * 2. 连接打开后 handler 订阅 Hub，并立即向客户端发送 live_ready。
 * 3. live_ready 的 start_record_seq 应从 Hub 当前 seq + 1 开始，说明不会重放打开前历史记录。
 *
 * 示例：
 *
 *   hub.publish(seq=10)
 *        |
 *        v
 *   upgrade(protocol_id=12) -> GetAccessInfo(project_id=1)
 *        |
 *        v
 *   live_ready {project_id:1, protocol_id:12, start_record_seq:11}
 */
TEST(TestWebProtocolInteraction, UpgradeSendsLiveReadyFromCurrentHubSeq)
{
    InteractionWsFixture f;
    f.Publish(MakeRecord(10, InteractionScope::kProtocol, 1, 12));

    f.Upgrade(1, 12);

    auto ready = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(ready["type"], "live_ready");
    EXPECT_EQ(ready["project_id"], 1);
    EXPECT_EQ(ready["protocol_id"], 12);
    EXPECT_EQ(ready["start_record_seq"], 11);
    EXPECT_GT(ready["session_id"].get<uint64_t>(), 0U);
}

/**
 * 测试思路：
 * 1. 连接 active 时，Hub 发布匹配协议项记录应推送 interaction。
 * 2. 客户端发送 pause(client_seq=1) 后，服务端返回 state=paused，后续匹配记录不再发送。
 * 3. 客户端发送 resume(client_seq=2) 后，服务端返回 state=resumed，新的匹配记录继续发送。
 *
 * 示例：
 *
 *   publish(seq=1) -> interaction
 *   pause #1       -> state(paused)
 *   publish(seq=2) -> no frame
 *   resume #2      -> state(resumed)
 *   publish(seq=3) -> interaction
 */
TEST(TestWebProtocolInteraction, PauseAndResumeGateHubInteractionDelivery)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    auto ready = SingleJsonTextFrame(f.peer.fd);
    const uint64_t session_id = ready["session_id"].get<uint64_t>();

    f.Publish(MakeRecord(1, InteractionScope::kProtocol, 1, 12));
    auto first = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(first["type"], "interaction");
    EXPECT_EQ(first["record"]["seq"], 1);

    f.SendClientJson({
        {"type", "pause"},
        {"session_id", session_id},
        {"client_seq", 1},
        {"timestamp", 1780000000100},
    });
    auto paused = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(paused["type"], "state");
    EXPECT_EQ(paused["state"], "paused");
    EXPECT_EQ(paused["accepted_seq"], 1);

    f.Publish(MakeRecord(2, InteractionScope::kProtocol, 1, 12));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());

    f.SendClientJson({
        {"type", "resume"},
        {"session_id", session_id},
        {"client_seq", 2},
        {"timestamp", 1780000000200},
    });
    auto resumed = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(resumed["type"], "state");
    EXPECT_EQ(resumed["state"], "resumed");
    EXPECT_EQ(resumed["accepted_seq"], 2);

    f.Publish(MakeRecord(3, InteractionScope::kProtocol, 1, 12));
    auto third = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(third["type"], "interaction");
    EXPECT_EQ(third["record"]["seq"], 3);
}

/**
 * 测试思路：
 * 1. 连接打开时默认 include_project_notice=true，同项目 project notice 应推送给协议项实时详情。
 * 2. 其他项目 notice 或同项目其他协议项 protocol record 都不能推送。
 * 3. 收到 close frame 后 handler 应退订，后续匹配记录也不能再发送到该 session。
 *
 * 示例：
 *
 *   live(project=1, protocol=12)
 *        + publish(project notice pj=1)  -> interaction
 *        + publish(protocol pc=13)       -> no frame
 *        + close                         -> unsubscribe
 *        + publish(protocol pc=12)       -> no frame
 */
TEST(TestWebProtocolInteraction, ProjectNoticeFilteringAndCloseUnsubscribes)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    (void)SingleJsonTextFrame(f.peer.fd);

    f.Publish(MakeRecord(1, InteractionScope::kProject, 1, 0, InteractionResult::kRouteNotFound));
    auto notice = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(notice["type"], "interaction");
    EXPECT_EQ(notice["record"]["scope"], "project");
    EXPECT_EQ(notice["record"]["protocol_id"], 0);
    EXPECT_EQ(notice["record"]["result"], "route_not_found");

    f.Publish(MakeRecord(2, InteractionScope::kProject, 2, 0, InteractionResult::kRouteNotFound));
    f.Publish(MakeRecord(3, InteractionScope::kProtocol, 1, 13));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());

    Buffer buf;
    AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kClose, {}));
    f.server.drainRemainingWebSocketBytes(f.conn, &buf, TimeStamp::Now());
    f.WaitForLoop();
    (void)ReadAvailable(f.peer.fd);

    f.Publish(MakeRecord(4, InteractionScope::kProtocol, 1, 12));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());
}

/**
 * 测试思路：
 * 1. interaction 记录可以携带二进制 sidecar，JSON 主消息只承载附件元数据。
 * 2. WebSocket 发送时必须先发 text frame，再紧跟 attachment binary frame。
 * 3. binary frame 前 4 字节是 header JSON 长度，后面依次是 header JSON 和原始附件 bytes。
 *
 * 示例：
 *
 *   publish(record.seq=21, attachments[0])
 *        |
 *        +-- text frame: {"type":"interaction","record":...attachments metadata...}
 *        |
 *        +-- binary frame: [header_len][{"type":"attachment","record_seq":21,...}][raw bytes]
 */
TEST(TestWebProtocolInteraction, InteractionAttachmentSendsJsonThenBinaryFrame)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    (void)SingleJsonTextFrame(f.peer.fd);

    const std::vector<uint8_t> attachment_bytes{0x89, 'P', 'N', 'G'};
    InteractionAttachmentRef ref;
    ref.attachment_id = "request.body:png";
    ref.side = "request";
    ref.flag = "request.body";
    ref.kind = InteractionPayloadKind::kImage;
    ref.size = attachment_bytes.size();
    ref.captured_size = attachment_bytes.size();
    ref.truncated = false;
    ref.binary_available = true;
    ref.sha1 = "test-sha1";

    auto record = MakeRecord(21, InteractionScope::kProtocol, 1, 12);
    record.request.body.kind = InteractionPayloadKind::kImage;
    record.request.body.expect_kind = InteractionPayloadKind::kBinary;
    record.request.body.size = attachment_bytes.size();
    record.request.body.captured_size = attachment_bytes.size();
    record.request.body.sha1 = ref.sha1;
    record.request.body.attachments.push_back(ref);
    record.binary_sidecars.push_back(BinarySidecar{
        .attachment_ref = ref,
        .bytes = std::make_shared<const std::vector<uint8_t>>(attachment_bytes),
    });

    f.Publish(std::move(record));

    const auto frames = DecodeServerFrames(ReadAvailable(f.peer.fd));
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0].opcode, WebSocketOpcode::kText);
    EXPECT_EQ(frames[1].opcode, WebSocketOpcode::kBinary);

    const auto interaction = nlohmann::json::parse(
        std::string(frames[0].payload.begin(), frames[0].payload.end()));
    EXPECT_EQ(interaction["type"], "interaction");
    EXPECT_EQ(interaction["record"]["seq"], 21);
    EXPECT_EQ(interaction["record"]["request"]["body"]["attachments"][0]["attachment_id"],
        ref.attachment_id);
    EXPECT_FALSE(interaction["record"].contains("binary_sidecars"));

    const auto &binary_payload = frames[1].payload;
    ASSERT_GE(binary_payload.size(), 4U);
    const uint32_t header_len = ReadUint32BE(binary_payload, 0);
    ASSERT_GE(binary_payload.size(), 4U + header_len);

    const std::string header_text(binary_payload.begin() + 4,
        binary_payload.begin() + 4 + header_len);
    const auto header = nlohmann::json::parse(header_text);
    EXPECT_EQ(header["type"], "attachment");
    EXPECT_EQ(header["record_seq"], 21);
    EXPECT_EQ(header["attachment_id"], ref.attachment_id);
    EXPECT_EQ(header["captured_size"], attachment_bytes.size());
    EXPECT_EQ(header["sha1"], ref.sha1);

    const std::vector<uint8_t> actual_bytes(binary_payload.begin() + 4 + header_len,
        binary_payload.end());
    EXPECT_EQ(actual_bytes, attachment_bytes);
}

/**
 * 测试思路：
 * 1. WebSocket upgrade 业务准备阶段如果协议项访问校验失败，应返回 403，不进入 101。
 * 2. 失败时 WebSocketServer 不应把 session 注册为 open，也不应触发 handler 的 Hub 订阅。
 * 3. 后续 Hub 发布匹配记录，客户端不能收到 live_ready 或 interaction。
 *
 * 示例：
 *
 *   GetAccessInfo(protocol_id=12) -> false
 *        |
 *        v
 *   HTTP 403, session !open, publish(seq=1) -> no frame
 */
TEST(TestWebProtocolInteraction, UpgradeAccessFailureReturnsForbiddenAndDoesNotSubscribe)
{
    InteractionWsFixture f;

    auto ctx = f.UpgradeDeniedByAccessFailure(12);

    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k403Forbidden);
    ASSERT_NE(f.session, nullptr);
    EXPECT_FALSE(f.session->isOpen());
    EXPECT_EQ(f.server.getSession(f.session->sessionId()), nullptr);
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());

    f.Publish(MakeRecord(1, InteractionScope::kProtocol, 1, 12));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());
}

/**
 * 测试思路：
 * 1. 前端即使传入 project_id，也不能影响订阅归属，真实 project_id 必须来自 GetAccessInfo(protocol_id)。
 * 2. 准备阶段返回 project=1 后，同协议项但 project=999 的记录不能推送。
 * 3. 同 protocol_id 且 project=1 的记录才会推送。
 *
 * 示例：
 *
 *   query: protocol_id=12, project_id=999
 *   GetAccessInfo -> project_id=1
 *        |
 *        +-- publish(project=999, pc=12) -> no frame
 *        +-- publish(project=1,   pc=12) -> interaction
 */
TEST(TestWebProtocolInteraction, UpgradeIgnoresClientProjectIdAndUsesAccessInfoProject)
{
    InteractionWsFixture f;

    EXPECT_CALL(*f.protocol_svc, GetAccessInfo(_, 12, _))
        .WillOnce(DoAll(SetArgReferee<2>(MakeAccessInfo(1, 12)), Return(true)));

    auto ctx = MakeUpgradeContext(12);
    ctx->request()->addQureyParam("project_id", "999");
    f.server.handleUpgrade(f.conn, ctx, [&f](WebSocketSessionPtr session, HttpContextPtr prepare_ctx) {
        f.session = session;
        return f.handler.onPrepare(std::move(session), std::move(prepare_ctx));
    });
    ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k101SwitchingProtocols);
    ASSERT_NE(f.session, nullptr);
    ASSERT_TRUE(f.session->isOpen());
    f.server.onOpen(f.conn);
    f.WaitForLoop();

    auto ready = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(ready["project_id"], 1);
    EXPECT_EQ(ready["protocol_id"], 12);

    f.Publish(MakeRecord(1, InteractionScope::kProtocol, 999, 12));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());

    f.Publish(MakeRecord(2, InteractionScope::kProtocol, 1, 12));
    auto interaction = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(interaction["type"], "interaction");
    EXPECT_EQ(interaction["record"]["project_id"], 1);
    EXPECT_EQ(interaction["record"]["protocol_id"], 12);
}

/**
 * 测试思路：
 * 1. handler 只接受 JSON text control message，且 type/session_id/client_seq 必须合法。
 * 2. 非法 JSON、未知 type、缺少 client_seq、session_id 不匹配都应返回业务 error。
 * 3. client_seq 跳号要返回 seq_gap，并带当前 accepted_seq/state，方便客户端重发或纠偏。
 *
 * 示例：
 *
 *   "{bad json"                         -> error bad_message
 *   {"type":"stop", ...}                -> error bad_message
 *   {"type":"pause", session_id=bad}    -> error bad_message
 *   {"type":"pause", client_seq=2}      -> error seq_gap accepted_seq=0
 */
TEST(TestWebProtocolInteraction, InvalidClientControlMessagesReturnBusinessErrors)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    auto ready = SingleJsonTextFrame(f.peer.fd);
    const uint64_t session_id = ready["session_id"].get<uint64_t>();

    f.SendClientFrame(WebSocketOpcode::kText, ToBytes("{bad json"));
    auto bad_json = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(bad_json["type"], "error");
    EXPECT_EQ(bad_json["code"], "bad_message");

    f.SendClientJson({
        {"type", "stop"},
        {"session_id", session_id},
        {"client_seq", 1},
        {"timestamp", 1780000000300},
    });
    auto unknown_type = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(unknown_type["type"], "error");
    EXPECT_EQ(unknown_type["code"], "bad_message");

    f.SendClientFrame(WebSocketOpcode::kText, ToBytes(nlohmann::json({
        {"type", "pause"},
        {"session_id", session_id},
        {"timestamp", 1780000000400},
    }).dump()));
    auto missing_seq = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(missing_seq["type"], "error");
    EXPECT_EQ(missing_seq["code"], "bad_message");

    f.SendClientJson({
        {"type", "pause"},
        {"session_id", session_id + 1},
        {"client_seq", 1},
        {"timestamp", 1780000000500},
    });
    auto bad_session = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(bad_session["type"], "error");
    EXPECT_EQ(bad_session["code"], "bad_message");

    f.SendClientJson({
        {"type", "pause"},
        {"session_id", session_id},
        {"client_seq", 2},
        {"timestamp", 1780000000600},
    });
    auto seq_gap = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(seq_gap["type"], "error");
    EXPECT_EQ(seq_gap["code"], "seq_gap");
    EXPECT_EQ(seq_gap["accepted_seq"], 0);
    EXPECT_EQ(seq_gap["state"], "resumed");
}

/**
 * 测试思路：
 * 1. 实时详情 handler 不定义额外 ping 业务语义，底层 WebSocketSession 应原样 pong。
 * 2. 客户端发送 masked ping frame 后，服务端返回 pong frame。
 * 3. pong payload 应与 ping payload 一致，不影响后续 interaction 推送。
 *
 * 示例：
 *
 *   client ping("kit")
 *        |
 *        v
 *   server pong("kit")
 */
TEST(TestWebProtocolInteraction, ClientPingReceivesPong)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    (void)SingleJsonTextFrame(f.peer.fd);

    f.SendClientFrame(WebSocketOpcode::kPing, ToBytes("kit"));

    const auto frames = DecodeServerFrames(ReadAvailable(f.peer.fd));
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames.front().opcode, WebSocketOpcode::kPong);
    EXPECT_EQ(std::string(frames.front().payload.begin(), frames.front().payload.end()), "kit");

    f.Publish(MakeRecord(1, InteractionScope::kProtocol, 1, 12));
    auto interaction = SingleJsonTextFrame(f.peer.fd);
    EXPECT_EQ(interaction["type"], "interaction");
    EXPECT_EQ(interaction["record"]["seq"], 1);
}

/**
 * 测试思路：
 * 1. V1 实时详情只接受 text control message，客户端 binary frame 应按不支持的数据类型关闭。
 * 2. 关闭时 handler 的 onError/onClose 路径应把 Hub subscriber 退订。
 * 3. 后续 Hub 发布匹配记录，已经失败关闭的 session 不能继续收到 interaction。
 *
 * 示例：
 *
 *   binary frame [1,2,3]
 *        |
 *        v
 *   close code=1003, session closed, publish(seq=2) -> no frame
 */
TEST(TestWebProtocolInteraction, ClientBinaryFrameClosesAndUnsubscribes)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    (void)SingleJsonTextFrame(f.peer.fd);

    f.SendClientFrame(WebSocketOpcode::kBinary, std::vector<uint8_t>{1, 2, 3});

    const auto frames = DecodeServerFrames(ReadAvailable(f.peer.fd));
    ASSERT_FALSE(frames.empty());
    EXPECT_EQ(frames.front().opcode, WebSocketOpcode::kClose);
    ASSERT_GE(frames.front().payload.size(), 2U);
    EXPECT_EQ(ReadUint16BE(frames.front().payload, 0),
        static_cast<uint16_t>(CloseCode::kUnsupportedDataType));
    EXPECT_FALSE(f.session->isOpen());

    f.Publish(MakeRecord(2, InteractionScope::kProtocol, 1, 12));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());
}

/**
 * 测试思路：
 * 1. TCP 层异常断开不会发送 close frame，但 WebSocketSession::onTcpDisconnected 会触发 close 回调。
 * 2. handler 的 close/error 清理必须退订 Hub subscriber。
 * 3. 断开后再发布匹配记录，不应继续写入该连接。
 *
 * 示例：
 *
 *   live open -> session.onTcpDisconnected()
 *        |
 *        v
 *   publish(project=1, protocol=12) -> no frame
 */
TEST(TestWebProtocolInteraction, TcpDisconnectUnsubscribesLiveSession)
{
    InteractionWsFixture f;
    f.Upgrade(1, 12);
    (void)SingleJsonTextFrame(f.peer.fd);

    f.session->onTcpDisconnected();
    f.WaitForLoop();
    EXPECT_FALSE(f.session->isOpen());

    f.Publish(MakeRecord(1, InteractionScope::kProtocol, 1, 12));
    EXPECT_TRUE(ReadAvailable(f.peer.fd).empty());
}

/**
 * 测试思路：
 * 1. 同一协议项可以有两个连接同时订阅，订阅状态应按 session 隔离。
 * 2. 一个连接 pause 后，只阻断该连接的 interaction，另一个连接仍应收到。
 * 3. 一个连接 close 后，另一个连接仍保持订阅并继续收到后续记录。
 *
 * 示例：
 *
 *   A live + B live
 *   A pause
 *   publish(seq=1) -> A no frame, B interaction
 *   A close
 *   publish(seq=2) -> B interaction
 */
TEST(TestWebProtocolInteraction, MultipleConnectionsKeepPauseAndCloseIsolated)
{
    auto shared_hub = std::make_shared<ProtocolInteractionHub>();
    InteractionWsFixture a(shared_hub);
    InteractionWsFixture b(shared_hub);

    a.Upgrade(1, 12);
    auto ready_a = SingleJsonTextFrame(a.peer.fd);
    const uint64_t session_a = ready_a["session_id"].get<uint64_t>();

    b.Upgrade(1, 12);
    (void)SingleJsonTextFrame(b.peer.fd);

    a.SendClientJson({
        {"type", "pause"},
        {"session_id", session_a},
        {"client_seq", 1},
        {"timestamp", 1780000000700},
    });
    auto paused = SingleJsonTextFrame(a.peer.fd);
    EXPECT_EQ(paused["state"], "paused");

    shared_hub->publish(MakeRecord(1, InteractionScope::kProtocol, 1, 12));
    a.WaitForLoop();
    b.WaitForLoop();

    EXPECT_TRUE(ReadAvailable(a.peer.fd).empty());
    auto b_first = SingleJsonTextFrame(b.peer.fd);
    EXPECT_EQ(b_first["type"], "interaction");
    EXPECT_EQ(b_first["record"]["seq"], 1);

    a.SendClientFrame(WebSocketOpcode::kClose, {});
    (void)ReadAvailable(a.peer.fd);
    shared_hub->publish(MakeRecord(2, InteractionScope::kProtocol, 1, 12));
    a.WaitForLoop();
    b.WaitForLoop();

    EXPECT_TRUE(ReadAvailable(a.peer.fd).empty());
    auto b_second = SingleJsonTextFrame(b.peer.fd);
    EXPECT_EQ(b_second["type"], "interaction");
    EXPECT_EQ(b_second["record"]["seq"], 2);
}
