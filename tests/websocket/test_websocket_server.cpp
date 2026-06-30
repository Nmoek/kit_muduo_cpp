/**
 * @file test_websocket_server.cpp
 * @brief websocket服务端Upgrade接入测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-30 00:00:00
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "base/event_loop_thread.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/event_loop.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_servlet.h"
#include "net/inet_address.h"
#include "net/tcp_connection.h"
#include "net/websocket/websocket_context.h"
#include "net/websocket/websocket_frame.h"
#include "net/websocket/websocket_server.h"
#include "net/websocket/websocket_session.h"
#include "net/websocket/websocket_util.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
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
#include <utility>
#include <unistd.h>
#include <vector>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_muduo::ws;

namespace {

constexpr const char *kClientKey = "dGhlIHNhbXBsZSBub25jZQ==";

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
    const std::vector<uint8_t> &payload)
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

struct ServerFixture
{
    std::unique_ptr<EventLoop> owned_loop;
    EventLoop *loop{nullptr};
    bool external_loop{false};
    FdGuard peer;
    TcpConnectionPtr conn;

    explicit ServerFixture(EventLoop *external_loop = nullptr)
    {
        if(external_loop)
        {
            loop = external_loop;
            this->external_loop = true;
        }
        else
        {
            owned_loop = std::make_unique<EventLoop>();
            loop = owned_loop.get();
        }

        FdGuard server;
        InetAddress server_peer_addr;
        InetAddress server_local_addr;
        MakeTcpConnectionPair(&server, &peer, &server_peer_addr, &server_local_addr);
        conn = std::make_shared<TcpConnection>(loop,
            "ws-server-test",
            server.release(),
            server_peer_addr,
            server_local_addr);
        conn->setConnectionCallback([](const TcpConnectionPtr&) {});

        if(this->external_loop)
        {
            auto conn_copy = conn;
            RunInLoopSync(loop, [conn_copy]() {
                conn_copy->connectEstablished();
            });
        }
        else
        {
            conn->connectEstablished();
        }
    }

    ~ServerFixture()
    {
        if(conn)
        {
            if(external_loop)
            {
                auto conn_copy = conn;
                conn.reset();
                RunInLoopSync(loop, [conn_copy]() {
                    conn_copy->connectDestroyed();
                });
            }
            else
            {
                conn->connectDestroyed();
            }
        }
    }
};

HttpContextPtr MakeUpgradeContext()
{
    auto ctx = std::make_shared<HttpContext>();
    auto req = ctx->request();
    req->setMethod(HttpRequest::Method::kGet);
    req->setPath("/ws/test");
    req->setVersion(Version::kHttp11);
    req->addHeader("Host", "localhost");
    req->addHeader("Connection", "Upgrade");
    req->addHeader("Upgrade", "websocket");
    req->addHeader("Sec-WebSocket-Version", "13");
    req->addHeader("Sec-WebSocket-Key", kClientKey);
    return ctx;
}

} // namespace

/*
测试思路：
1. 合法 Upgrade 请求应通过 WebSocketServer 校验。
2. on_cb 可以注册业务回调，返回 true 后 session 应打开并写入 session 表。
3. ctx->response 应被设置为 HTTP/1.1 101，并带上 Sec-WebSocket-Accept。
4. TcpConnection::context 应切换成 WebSocketContext。

示例：
  GET + Upgrade headers
      |
      v
  on_cb -> bind context -> kOpen -> 101 Switching Protocols
*/
TEST(TestWebSocketServer, HandleUpgradeAcceptsValidRequest)
{
    WebSocketServer server;
    ServerFixture f;
    auto ctx = MakeUpgradeContext();
    WebSocketSessionPtr opened_session;
    bool on_called = false;

    server.handleUpgrade(f.conn, ctx, [&](WebSocketSessionPtr session, HttpContextPtr cb_ctx) {
        EXPECT_EQ(cb_ctx, ctx);
        on_called = true;
        opened_session = session;
        session->setWSTextMessageCb([](WebSocketSessionPtr session, const std::string &text) {
            session->sendText(text);
        });
        return true;
    });

    ASSERT_TRUE(on_called);
    ASSERT_NE(opened_session, nullptr);
    EXPECT_TRUE(opened_session->isOpen());
    EXPECT_EQ(server.getSession(opened_session->sessionId()), opened_session);

    auto resp = ctx->response();
    EXPECT_EQ(resp->version().toInt(), Version::kHttp11);
    EXPECT_EQ(resp->stateCode().toInt(), StateCode::k101SwitchingProtocols);
    EXPECT_TRUE(resp->upgrade());
    EXPECT_EQ(resp->getHeader("Sec-WebSocket-Accept"), BuildWebSocketAcceptKey(kClientKey));

    auto ws_ctx = std::static_pointer_cast<WebSocketContext>(f.conn->getContext());
    ASSERT_NE(ws_ctx, nullptr);
    EXPECT_EQ(ws_ctx->lockSession(), opened_session);
}

/*
测试思路：
1. Upgrade 校验失败时不能创建 session，也不能切换连接 context。
2. 缺少 Sec-WebSocket-Key 时应返回 400 Bad Request，并关闭连接语义。

示例：
  missing Sec-WebSocket-Key
      |
      v
  400 Bad Request, on_cb not called
*/
TEST(TestWebSocketServer, HandleUpgradeRejectsInvalidHandshake)
{
    WebSocketServer server;
    ServerFixture f;
    auto ctx = MakeUpgradeContext();
    ctx->request()->addHeader("Sec-WebSocket-Key", "invalid");
    bool on_called = false;

    server.handleUpgrade(f.conn, ctx, [&](WebSocketSessionPtr, HttpContextPtr) {
        on_called = true;
        return true;
    });

    EXPECT_FALSE(on_called);
    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k400BadRequest);
    EXPECT_TRUE(ctx->response()->connectionClosed());
    EXPECT_EQ(f.conn->getContext(), nullptr);
}

/*
测试思路：
1. WebSocketServer 先完成握手校验，再交给业务 on_cb 做权限、参数、订阅准备。
2. on_cb 返回 false 表示业务拒绝升级，应返回 500，且不能保存 session 或切换 context。

示例：
  valid handshake + on_cb false
      |
      v
  500 Internal Server Error, session not open
*/
TEST(TestWebSocketServer, HandleUpgradeRejectsWhenBusinessCallbackReturnsFalse)
{
    WebSocketServer server;
    ServerFixture f;
    auto ctx = MakeUpgradeContext();
    WebSocketSessionPtr rejected_session;

    server.handleUpgrade(f.conn, ctx, [&](WebSocketSessionPtr session, HttpContextPtr) {
        rejected_session = session;
        return false;
    });

    ASSERT_NE(rejected_session, nullptr);
    EXPECT_EQ(rejected_session->state(), WebSocketSessionState::kOpening);
    EXPECT_EQ(server.getSession(rejected_session->sessionId()), nullptr);
    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k500InternalServerError);
    EXPECT_TRUE(ctx->response()->connectionClosed());
    EXPECT_EQ(f.conn->getContext(), nullptr);
}

/*
测试思路：
1. bindSessionToConnection 后，剩余 websocket bytes 应由 drainRemainingWebSocketBytes 投递到 WebSocketServer::onMessage。
2. text 回调里 echo 文本，说明 drain 的剩余 bytes 已经走到 session->onMessage。

示例：
  upgrade ok + buffer contains masked text "echo"
      |
      v
  drainRemainingWebSocketBytes -> text_cb -> server text frame "echo"
*/
TEST(TestWebSocketServer, DrainRemainingBytesDispatchesToWebSocketSession)
{
    WebSocketServer server;
    EventLoopThread loop_thread(nullptr, "ws-drain-test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);
    ServerFixture f(loop);
    auto ctx = MakeUpgradeContext();
    WebSocketSessionPtr opened_session;
    Buffer buf;

    server.handleUpgrade(f.conn, ctx, [&](WebSocketSessionPtr session, HttpContextPtr) {
        opened_session = session;
        session->setWSTextMessageCb([](WebSocketSessionPtr session, const std::string &text) {
            session->sendText(text);
        });
        return true;
    });
    ASSERT_NE(opened_session, nullptr);
    ASSERT_EQ(ctx->response()->stateCode().toInt(), StateCode::k101SwitchingProtocols);

    AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kText, ToBytes("echo")));
    server.drainRemainingWebSocketBytes(f.conn, &buf, TimeStamp(1));

    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kText, "echo");
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. WebSocketServer 的 clear_cb 应在 session 关闭时从 session 表删除记录。
2. 这能防止长连接断开后 getSession 仍返回陈旧 session。

示例：
  upgrade ok -> server.getSession(id) exists
  peer close -> clear_cb
      |
      v
  server.getSession(id) == nullptr
*/
TEST(TestWebSocketServer, PeerCloseRemovesSessionFromServerMap)
{
    WebSocketServer server;
    EventLoopThread loop_thread(nullptr, "ws-peer-close-test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);
    ServerFixture f(loop);
    auto ctx = MakeUpgradeContext();
    WebSocketSessionPtr opened_session;
    Buffer buf;

    server.handleUpgrade(f.conn, ctx, [&](WebSocketSessionPtr session, HttpContextPtr) {
        opened_session = session;
        return true;
    });
    ASSERT_NE(opened_session, nullptr);
    ASSERT_NE(server.getSession(opened_session->sessionId()), nullptr);

    AppendToBuffer(&buf, BuildClientFrame(WebSocketOpcode::kClose, {}));
    server.drainRemainingWebSocketBytes(f.conn, &buf, TimeStamp(1));

    const auto expected = BuildWebSocketFrameBytes(WebSocketOpcode::kClose,
        BuildClosePayload(CloseCode::kNormalShutdown, "peer close"));
    EXPECT_EQ(ReadExact(f.peer.fd, expected.size()), expected);
    RunInLoopSync(loop, []() {});
    EXPECT_EQ(opened_session->state(), WebSocketSessionState::kClosed);
    EXPECT_EQ(server.getSession(opened_session->sessionId()), nullptr);
}

/*
测试思路：
1. 第 7 点约定 WebSocket endpoint 复用普通 GET route 分发。
2. 这里直接用 HttpServletDispatch 注册等价 GET handler，并用真实 loopback TCP 连接构造 TcpConnection。
3. dispatch 命中该路由后，应调用 WebSocketServer::handleUpgrade 并把响应改成 101。

示例：
  dispatch.addRoute(GET, "/ws/test", handleUpgrade)
  dispatch GET /ws/test
      |
      v
  cb called, response 101
*/
TEST(TestWebSocketServer, ServletDispatchRoutesWebSocketUpgradeHandler)
{
    WebSocketServer server;
    ServerFixture f;
    auto ctx = MakeUpgradeContext();
    HttpServletDispatch dispatch;
    bool on_called = false;

    ASSERT_TRUE(dispatch.addRoute(ExpectHttpMethods::Get,
        "/ws/test",
        [&server, &ctx, &on_called](TcpConnectionPtr conn, HttpContextPtr cb_ctx) {
            server.handleUpgrade(conn, cb_ctx, [&](WebSocketSessionPtr, HttpContextPtr on_ctx) {
                EXPECT_EQ(on_ctx, ctx);
                on_called = true;
                return true;
            });
        }).ok());

    dispatch.handle(f.conn, ctx);

    EXPECT_TRUE(on_called);
    EXPECT_EQ(ctx->response()->stateCode().toInt(), StateCode::k101SwitchingProtocols);
    EXPECT_EQ(ctx->response()->getHeader("Sec-WebSocket-Accept"), BuildWebSocketAcceptKey(kClientKey));
    EXPECT_NE(std::static_pointer_cast<WebSocketContext>(f.conn->getContext()), nullptr);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
