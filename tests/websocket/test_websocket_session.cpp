/**
 * @file test_websocket_session.cpp
 * @brief websocket会话状态和收发行为测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-30 00:00:00
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/event_loop.h"
#include "net/inet_address.h"
#include "net/tcp_connection.h"
#include "net/websocket/websocket_context.h"
#include "net/websocket/websocket_frame.h"
#include "net/websocket/websocket_session.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace kit_muduo;
using namespace kit_muduo::ws;

namespace {

struct FdGuard
{
    explicit FdGuard(int32_t fd = -1)
        :fd(fd)
    {}

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

std::vector<uint8_t> ReadExact(int32_t fd, size_t len)
{
    std::vector<uint8_t> out(len);
    size_t offset = 0;
    while(offset < len)
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ready = ::poll(&pfd, 1, 2000);
        EXPECT_GT(ready, 0) << ::strerror(errno);
        if(ready <= 0)
        {
            out.resize(offset);
            return out;
        }

        ssize_t n = ::read(fd, out.data() + offset, len - offset);
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        EXPECT_GT(n, 0) << ::strerror(errno);
        if(n <= 0)
        {
            out.resize(offset);
            return out;
        }
        offset += static_cast<size_t>(n);
    }
    return out;
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

std::vector<uint8_t> BuildClientFrame(WebSocketOpcode opcode,
    const std::vector<uint8_t> &payload,
    bool fin = true)
{
    const std::vector<uint8_t> mask{0x21, 0x43, 0x65, 0x87};
    std::vector<uint8_t> out;

    uint8_t byte0 = static_cast<uint8_t>(opcode) & 0x0F;
    if(fin)
    {
        byte0 |= 0x80;
    }
    out.push_back(byte0);

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

void AppendToBuffer(Buffer *buf, const std::vector<uint8_t> &bytes)
{
    buf->append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

struct SessionFixture
{
    EventLoop loop;
    FdGuard peer;
    TcpConnectionPtr conn;
    WebSocketSessionPtr session;

    explicit SessionFixture(uint64_t session_id = 7)
    {
        FdGuard server;
        InetAddress server_peer_addr;
        InetAddress server_local_addr;
        MakeTcpConnectionPair(&server, &peer, &server_peer_addr, &server_local_addr);
        conn = std::make_shared<TcpConnection>(&loop,
            "ws-session-test",
            server.release(),
            server_peer_addr,
            server_local_addr);
        conn->setConnectionCallback([](const TcpConnectionPtr&) {});
        conn->connectEstablished();
        session = std::make_shared<WebSocketSession>(session_id, conn);
    }

    ~SessionFixture()
    {
        if(conn)
        {
            conn->connectDestroyed();
        }
    }
};

} // namespace

/*
测试思路：
1. session 初始状态为 kOpening，只能通过 open() 切到 kOpen。
2. open() 是一次性状态迁移，重复 open 应失败，避免状态机回退。

示例：
  kOpening --open()--> kOpen
  kOpen    --open()--> false
*/
TEST(TestWebSocketSession, OpenTransitionsOnlyOnce)
{
    SessionFixture f;

    ASSERT_EQ(f.session->state(), WebSocketSessionState::kOpening);
    ASSERT_TRUE(f.session->open());
    EXPECT_TRUE(f.session->isOpen());
    EXPECT_FALSE(f.session->open());
    EXPECT_EQ(f.session->state(), WebSocketSessionState::kOpen);
}

/*
测试思路：
1. sendText 只允许在 kOpen 状态发送。
2. 打开 session 后，发送 "hello" 应在对端读到服务端 text frame。

示例：
  session->sendText("hello")
      |
      v
  0x81 0x05 h e l l o
*/
TEST(TestWebSocketSession, SendTextWritesServerTextFrame)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());

    f.session->sendText("hello");

    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kText, "hello");
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
}

/*
测试思路：
1. sendBinary 是服务端主动推送附件二进制帧的基础能力。
2. 打开 session 后，binary payload 应用 opcode=0x02 发送，payload 原样保留。

示例：
  session->sendBinary({0x01, 0x02})
      |
      v
  0x82 0x02 0x01 0x02
*/
TEST(TestWebSocketSession, SendBinaryWritesServerBinaryFrame)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());

    const std::vector<uint8_t> payload{0x01, 0x02, 0xFF};
    f.session->sendBinary(payload);

    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kBinary, payload);
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
}

/*
测试思路：
1. sendMessageGroup 应按文档要求一次发送 Text + Binary... 的连续 frame。
2. JSON 使用 text opcode，后续附件使用 binary opcode。

示例：
  json "{}" + [bin1, bin2]
      |
      v
  text("{}") binary(bin1) binary(bin2)
*/
TEST(TestWebSocketSession, SendMessageGroupWritesTextThenBinaryFrames)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());

    auto bin1 = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{0x10, 0x11});
    auto bin2 = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{0x20});
    f.session->sendMessageGroup("{}", {bin1, bin2});

    std::vector<uint8_t> expected = BuildWebSocketFrameBytes(WebSocketOpcode::kText, "{}");
    const auto frame1 = BuildWebSocketFrameBytes(WebSocketOpcode::kBinary, *bin1);
    const auto frame2 = BuildWebSocketFrameBytes(WebSocketOpcode::kBinary, *bin2);
    expected.insert(expected.end(), frame1.begin(), frame1.end());
    expected.insert(expected.end(), frame2.begin(), frame2.end());

    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
}

/*
测试思路：
1. sendMessageGroup 遇到空 binary frame 指针时应放弃整组发送。
2. 该路径用于防止发送半组业务消息，避免前端收到 JSON 但附件缺失。

示例：
  json "{}" + [nullptr]
      |
      v
  no bytes sent
*/
TEST(TestWebSocketSession, SendMessageGroupSkipsWholeGroupWhenBinaryFrameIsNull)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());
    ASSERT_EQ(::fcntl(f.peer.fd, F_SETFL, O_NONBLOCK), 0);

    f.session->sendMessageGroup("{}", {nullptr});

    uint8_t byte = 0;
    EXPECT_EQ(::read(f.peer.fd, &byte, sizeof(byte)), -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

/*
测试思路：
1. 客户端 text frame 应由 WebSocketContext 解包后投递给 session 的 text 回调。
2. onMessage 会循环处理 buffer 中所有完整 frame。

示例：
  masked text "hello"
      |
      v
  text_cb(session, "hello")
*/
TEST(TestWebSocketSession, OnMessageDispatchesTextCallback)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());
    auto ctx = std::make_shared<WebSocketContext>(f.session);
    Buffer buf;
    std::string received;

    f.session->setWSTextMessageCb([&](WebSocketSessionPtr session, const std::string &text) {
        EXPECT_EQ(session->sessionId(), f.session->sessionId());
        received = text;
    });

    AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kText, ToBytes("hello")));
    f.session->onMessage(ctx, &buf, TimeStamp(1));

    EXPECT_EQ(received, "hello");
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. 客户端 ping frame 属于控制帧，不投递业务 text 回调。
2. session 应立即回复 pong，payload 原样带回。

示例：
  client ping("hb")
      |
      v
  server pong("hb")
*/
TEST(TestWebSocketSession, OnMessageRepliesPongForPing)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());
    auto ctx = std::make_shared<WebSocketContext>(f.session);
    Buffer buf;

    AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kPing, ToBytes("hb")));
    f.session->onMessage(ctx, &buf, TimeStamp(1));

    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kPong, "hb");
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. 客户端发送 close frame 后，session 应回复 close frame 并进入 kClosed。
2. close_cb 和 clear_cb 都应只触发一次，用于业务清理和 WebSocketServer session 表清理。

示例：
  client close
      |
      v
  server close(1000, "peer close") -> kClosed -> callbacks once
*/
TEST(TestWebSocketSession, OnMessageHandlesPeerCloseAndFiresCallbacks)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());
    auto ctx = std::make_shared<WebSocketContext>(f.session);
    Buffer buf;
    int close_count = 0;
    int clear_count = 0;
    uint64_t clear_session_id = 0;

    f.session->setCloseCb([&](WebSocketSessionPtr session) {
        EXPECT_EQ(session->sessionId(), f.session->sessionId());
        ++close_count;
    });
    f.session->setWSClearCb([&](uint64_t session_id) {
        ++clear_count;
        clear_session_id = session_id;
    });

    AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kClose, {}));
    f.session->onMessage(ctx, &buf, TimeStamp(1));

    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kClose,
        BuildClosePayload(CloseCode::kNormalShutdown, "peer close"));
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
    EXPECT_EQ(f.session->state(), WebSocketSessionState::kClosed);
    EXPECT_EQ(close_count, 1);
    EXPECT_EQ(clear_count, 1);
    EXPECT_EQ(clear_session_id, f.session->sessionId());

    f.session->onTcpDisconnected();
    EXPECT_EQ(close_count, 1);
    EXPECT_EQ(clear_count, 1);
}

/*
测试思路：
1. parseFrame 发现协议错误时，session::onMessage 应调用 fail。
2. fail 应触发 error_cb，发送 close frame，然后进入 kClosed 并触发 close/clear。

示例：
  unmasked client frame
      |
      v
  error_cb(1002) + close frame + kClosed
*/
TEST(TestWebSocketSession, OnMessageFailsOnProtocolError)
{
    SessionFixture f;
    ASSERT_TRUE(f.session->open());
    auto ctx = std::make_shared<WebSocketContext>(f.session);
    Buffer buf;
    CloseCode error_code = CloseCode::kNormalShutdown;
    std::string error_reason;
    int close_count = 0;
    int clear_count = 0;

    f.session->setWSErrorCb([&](WebSocketSessionPtr session, CloseCode code, const std::string &reason) {
        EXPECT_EQ(session->sessionId(), f.session->sessionId());
        error_code = code;
        error_reason = reason;
    });
    f.session->setCloseCb([&](WebSocketSessionPtr) { ++close_count; });
    f.session->setWSClearCb([&](uint64_t) { ++clear_count; });

    std::vector<uint8_t> unmasked{0x81, 0x00, 0x00};
    AppendToBuffer(&buf, unmasked);
    f.session->onMessage(ctx, &buf, TimeStamp(1));

    EXPECT_EQ(error_code, CloseCode::kProtocolError);
    EXPECT_EQ(error_reason, "client frame must be masked");
    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kClose,
        BuildClosePayload(CloseCode::kProtocolError, "client frame must be masked"));
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
    EXPECT_EQ(f.session->state(), WebSocketSessionState::kClosed);
    EXPECT_EQ(close_count, 1);
    EXPECT_EQ(clear_count, 1);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
