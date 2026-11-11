#include "../../support/tls_test_support.h"
#include "base/event_loop_thread.h"
#include "domain/http_project_server.h"
#include "domain/https_project_server.h"
#include "net/tls_connection.h"
#include "domain/http_protocol_item.h"
#include "domain/project_server_factory.h"
#include "../../support/scoped_https_creator.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "domain/runtime_loop_pool.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

using namespace kit_muduo;
using namespace kit_domain;
using namespace kit_test;
using namespace std::chrono_literals;

namespace {

class HttpsServerTest : public testing::Test
{
protected:
    static void SetUpTestSuite() { certs = std::make_unique<TestCertificates>(); }
    static void TearDownTestSuite() { certs.reset(); }

    void SetUp() override
    {
        loop = thread.startLoop();
        std::promise<void> ready;
        loop->queueInLoop([this, &ready] {
            server = std::make_unique<TcpServer>(loop, InetAddress(0, "127.0.0.1"), "tls-test");
            server->setConnectionFactory([context = TlsContext::Create(certs->config())](EventLoop* connection_loop,
                const std::string& name, int32_t fd, const InetAddress& peer_addr, const InetAddress& local_addr) {
                return std::make_shared<TlsConnection>(connection_loop, name, fd, peer_addr, local_addr, context);
            });
            server->setConnectionCallback([this](TcpConnectionPtr conn) {
                if(conn->connected())
                {
                    ++connections;
                    const int size = 4096;
                    ::setsockopt(conn->fd(), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
                }
                else ++disconnections;
            });
            server->setMessageCallback([this](TcpConnectionPtr conn, Buffer* buffer, TimeStamp) {
                const std::string text = buffer->resetAllAsString();
                if(text == "large" || text == "large-close")
                {
                    conn->send(std::string(2 * 1024 * 1024, 'A'));
                    // 第一次 SSL_write 已可能 WANT_WRITE；再次 send 不得改变重试数据。
                    conn->send(std::string(64 * 1024, 'B'));
                    if(text == "large-close") conn->shutdown();
                }
                else
                {
                    conn->send(text);
                    if(text == "close") conn->shutdown();
                }
            });
            server->start();
            port = server->getBindAddr().toPort();
            ready.set_value();
        });
        ASSERT_EQ(ready.get_future().wait_for(3s), std::future_status::ready);
    }

    void TearDown() override
    {
        server->stop();
        server.reset();
    }

    void barrier()
    {
        std::promise<void> done;
        loop->queueInLoop([&done] { done.set_value(); });
        ASSERT_EQ(done.get_future().wait_for(3s), std::future_status::ready);
    }

    inline static std::unique_ptr<TestCertificates> certs;
    EventLoopThread thread{nullptr, "tls-test-loop"};
    EventLoop* loop{nullptr};
    std::unique_ptr<TcpServer> server;
    uint16_t port{0};
    std::atomic_int connections{0};
    std::atomic_int disconnections{0};
};

// 思路：启动前加载证书时即拒绝无效配置。例：CA 私钥搭配服务端证书不能创建上下文。
TEST_F(HttpsServerTest, RejectsMissingRelativeInvalidAndMismatchedPem)
{
    EXPECT_THROW(TlsContext::Create({}), std::invalid_argument);
    EXPECT_THROW(TlsContext::Create({"relative.pem", certs->keyFile()}), std::invalid_argument);
    EXPECT_THROW(TlsContext::Create({"/tmp/kit-https-no-such-cert.pem", certs->keyFile()}), std::runtime_error);
    EXPECT_THROW(TlsContext::Create({certs->certFile(), certs->caKeyFile()}), std::runtime_error);
    EXPECT_THROW(TlsContext::Create({certs->keyFile(), certs->keyFile()}), std::runtime_error);
    EXPECT_THROW(TlsContext::Create({certs->certFile(), certs->certFile()}), std::runtime_error);
    EXPECT_NO_THROW(TlsContext::Create(certs->config()));
}

// 思路：分别限制客户端版本为 1.2/1.3，并验证信任和 ALPN。例：两种连接都回显 hello。
TEST_F(HttpsServerTest, SupportsTls12And13WithTrustedCaAndHttp11Alpn)
{
    for(int version : {TLS1_2_VERSION, TLS1_3_VERSION})
    {
        TlsClient client(port, certs->caFile(), "localhost", version);
        ASSERT_TRUE(client.connect());
        EXPECT_EQ(SSL_version(client.ssl()), version);
        EXPECT_EQ(SSL_get_verify_result(client.ssl()), X509_V_OK);
        const unsigned char* alpn = nullptr;
        unsigned int len = 0;
        SSL_get0_alpn_selected(client.ssl(), &alpn, &len);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(alpn), len), "http/1.1");
        ASSERT_TRUE(client.write("hello"));
        EXPECT_EQ(client.read(5), "hello");
        EXPECT_GE(SSL_shutdown(client.ssl()), 0);
    }
}

// 思路：模拟常见客户端失败配置，失败不能进入业务回调。例：h2-only 或错误域名握手被拒。
TEST_F(HttpsServerTest, RejectsUntrustedWrongHostAndUnsupportedAlpn)
{
    {
        TlsClient client(port, "");
        EXPECT_FALSE(client.connect());
    }
    {
        TlsClient client(port, certs->caFile(), "wrong.example");
        EXPECT_FALSE(client.connect());
    }
    {
        TlsClient client(port, certs->caFile(), "localhost", 0, "h2");
        EXPECT_FALSE(client.connect());
    }
    barrier();
    EXPECT_EQ(connections.load(), 0);
    EXPECT_EQ(disconnections.load(), 0);
    TlsClient healthy(port, certs->caFile());
    ASSERT_TRUE(healthy.connect());
    ASSERT_TRUE(healthy.write("ok"));
    EXPECT_EQ(healthy.read(2), "ok");
}

// 思路：不提供 ALPN 的 HTTP/1.x 客户端仍可连接。例：三段写入和后续请求均逐字回显。
TEST_F(HttpsServerTest, FragmentedRequestsAndPersistentConnectionWithoutAlpn)
{
    TlsClient client(port, certs->caFile(), "localhost", 0, "");
    ASSERT_TRUE(client.connect());
    for(const auto& part : {"GET /", "test HTTP/1.1\r\n", "Host: localhost\r\n\r\n", "next"})
    {
        ASSERT_TRUE(client.write(part));
        EXPECT_EQ(client.read(std::strlen(part)), part);
    }
    EXPECT_EQ(connections.load(), 1);
}

// 思路：一个连接不发 ClientHello，另一个连接照常握手和收发，证明没有阻塞 loop。
// 例：silent TCP connect ──等待──；TLS connect -> echo(ok) 在同一个 loop 完成。
TEST_F(HttpsServerTest, IncompleteHandshakeDoesNotBlockOtherConnections)
{
    const int silent = ConnectLoopback(port);
    TlsClient client(port, certs->caFile());
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.write("ok"));
    EXPECT_EQ(client.read(2), "ok");
    EXPECT_EQ(connections.load(), 1);
    ::close(silent);
}

// 思路：明文 HTTP 不应到达 HTTP parser，且不产生业务连接回调。例：GET / 被 TLS 层拒绝。
TEST_F(HttpsServerTest, PlaintextIsRejectedBeforeBusinessCallback)
{
    const int fd = ConnectLoopback(port);
    const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ASSERT_EQ(::send(fd, request.data(), request.size(), MSG_NOSIGNAL), request.size());
    char buffer[128];
    EXPECT_LE(::recv(fd, buffer, sizeof(buffer), 0), 0);
    ::close(fd);
    barrier();
    EXPECT_EQ(connections.load(), 0);
    EXPECT_EQ(disconnections.load(), 0);
}

// 思路：慢客户端造成内核发送缓冲区满，随后追加 send。例：2 MiB A 后 64 KiB B 不丢失、不乱序。
TEST_F(HttpsServerTest, SlowReaderPreservesQueuedWritesAndRetryBuffer)
{
    TlsClient client(port, certs->caFile());
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.write("large"));
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(client.read(2 * 1024 * 1024 + 64 * 1024),
        std::string(2 * 1024 * 1024, 'A') + std::string(64 * 1024, 'B'));
    ASSERT_TRUE(client.write("after"));
    EXPECT_EQ(client.read(5), "after");
}

// 思路：先等待响应和 close_notify，再由客户端完成关闭。例：close -> echo -> SSL_read=0。
TEST_F(HttpsServerTest, GracefulShutdownDrainsResponseAndSendsCloseNotify)
{
    TlsClient client(port, certs->caFile());
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.write("close"));
    EXPECT_EQ(client.read(5), "close");
    char byte;
    const int result = SSL_read(client.ssl(), &byte, 1);
    EXPECT_EQ(SSL_get_error(client.ssl(), result), SSL_ERROR_ZERO_RETURN);
    EXPECT_EQ(SSL_shutdown(client.ssl()), 1);
}

// 思路：业务仅持有基类指针，从非 Loop 线程调用所有 send 重载及 shutdown，仍须走 TLS。
// 例：vector<char> first + vector<uint8_t> second + string third -> 解密得到 firstsecondthird，随后 close_notify。
TEST_F(HttpsServerTest, BasePointerDispatchesCrossThreadSendAndShutdownToTls)
{
    auto connected = std::make_shared<std::promise<TcpConnectionPtr>>();
    auto disconnected = std::make_shared<std::promise<void>>();
    auto connection = connected->get_future();
    auto closed = disconnected->get_future();
    loop->queueInLoop([this, connected, disconnected] {
        server->setConnectionCallback([this, connected, disconnected](TcpConnectionPtr conn) {
            if(conn->connected())
            {
                ++connections;
                connected->set_value(conn);
            }
            else
            {
                ++disconnections;
                disconnected->set_value();
            }
        });
    });
    barrier();
    EXPECT_THROW(server->setConnectionFactory({}), std::logic_error);
    TlsClient client(port, certs->caFile());
    ASSERT_TRUE(client.connect());
    ASSERT_EQ(connection.wait_for(3s), std::future_status::ready);
    TcpConnectionPtr conn = connection.get();
    ASSERT_NE(std::dynamic_pointer_cast<TlsConnection>(conn), nullptr);
    ASSERT_FALSE(conn->getLoop()->isInLoopThread());
    conn->send(std::vector<char>{'f', 'i', 'r', 's', 't'});
    conn->send(std::vector<uint8_t>{'s', 'e', 'c', 'o', 'n', 'd'});
    conn->send(std::string("third"));
    conn->shutdown();
    EXPECT_EQ(client.read(16), "firstsecondthird");
    char byte;
    const int result = SSL_read(client.ssl(), &byte, 1);
    EXPECT_EQ(SSL_get_error(client.ssl(), result), SSL_ERROR_ZERO_RETURN);
    EXPECT_EQ(SSL_shutdown(client.ssl()), 1);
    ASSERT_EQ(closed.wait_for(3s), std::future_status::ready);
    EXPECT_EQ(connections.load(), 1);
    EXPECT_EQ(disconnections.load(), 1);
}

// 思路：关闭请求发生在大响应尚未发完时，必须等发送队列排空才发送 close_notify。
// 例：large-close -> 慢读 2 MiB A + 64 KiB B -> close_notify，响应不能被截断。
TEST_F(HttpsServerTest, ShutdownWaitsForBackpressuredResponseToDrain)
{
    TlsClient client(port, certs->caFile());
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.write("large-close"));
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(client.read(2 * 1024 * 1024 + 64 * 1024),
        std::string(2 * 1024 * 1024, 'A') + std::string(64 * 1024, 'B'));
    char byte;
    const int result = SSL_read(client.ssl(), &byte, 1);
    EXPECT_EQ(SSL_get_error(client.ssl(), result), SSL_ERROR_ZERO_RETURN);
    EXPECT_EQ(SSL_shutdown(client.ssl()), 1);
}

// 思路：客户端收到 close_notify 后不回应，服务端也必须有界关闭。例：约 2 秒后 TCP EOF。
TEST_F(HttpsServerTest, ShutdownTimeoutClosesUnresponsivePeer)
{
    TlsClient client(port, certs->caFile());
    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.write("close"));
    EXPECT_EQ(client.read(5), "close");
    char byte;
    const int result = SSL_read(client.ssl(), &byte, 1);
    ASSERT_EQ(SSL_get_error(client.ssl(), result), SSL_ERROR_ZERO_RETURN);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(::recv(client.fd(), &byte, 1, 0), 0);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 3s);
}

// 思路：不发送 ClientHello 的连接在 10 秒后关闭且没有业务回调，例：裸 TCP -> 10 秒 -> EOF。
TEST_F(HttpsServerTest, HandshakeTimeoutClosesSilentPeer)
{
    const int fd = ConnectLoopback(port, 13);
    const auto start = std::chrono::steady_clock::now();
    char byte;
    EXPECT_EQ(::recv(fd, &byte, 1, 0), 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, 9s);
    EXPECT_LT(elapsed, 13s);
    ::close(fd);
    EXPECT_EQ(connections.load(), 0);
}

// 思路：活动连接、未完成握手和大响应中断都不能阻止 stop；例：stop 在 1 秒内完成。
TEST_F(HttpsServerTest, ForceStopCleansActiveAndHandshakingConnections)
{
    const int silent = ConnectLoopback(port);
    {
        TlsClient broken(port, certs->caFile());
        ASSERT_TRUE(broken.connect());
        ASSERT_TRUE(broken.write("large"));
    }
    TlsClient active(port, certs->caFile());
    ASSERT_TRUE(active.connect());
    ASSERT_TRUE(active.write("ok"));
    EXPECT_EQ(active.read(2), "ok");
    const auto start = std::chrono::steady_clock::now();
    server->stop();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    ::close(silent);
    barrier();
    EXPECT_EQ(connections.load(), disconnections.load());
}

// 思路：真实 HTTPS 工厂创建 HttpsProjectServer 并复用 HTTP 业务，验证匹配、修改、404 和交互类型，停止后复用端口。
// 例：GET /secure -> 200 + {"ok":true}；改成 /updated 后旧路由 404，新路由成功。
// 创建器替换默认配置后能启动；暂移证书后重启失败，恢复后再次启动，证明每次创建重新加载证书。
TEST(HttpsProjectServerTest, FactoryRoutesUpdatesObservationsAndRestart)
{
    TestCertificates certs;
    RuntimeLoopPool pool(2);
    Project project{.m_ctime = TimeStamp::Now()};
    project.m_id = 6101;
    project.m_protocolType = ProtocolType::kHttps;
    project.m_listenPort = 0;
    auto lease = pool.acquire(project.m_id);
    ASSERT_TRUE(lease.ok());
    EXPECT_THROW(ProjectServerFactory::Create(project, lease.val), std::invalid_argument);
    kit_test::ScopedHttpsCreator creator(certs.config());
    auto server = ProjectServerFactory::Create(project, lease.val);
    ASSERT_NE(server, nullptr);
    ASSERT_NE(std::dynamic_pointer_cast<HttpsProjectServer>(server), nullptr);
    std::mutex mutex;
    std::vector<ProtocolInteractionObservation> observations;
    server->setObserveCallback([&](ProtocolInteractionObservation obs) {
        std::lock_guard<std::mutex> lock(mutex);
        observations.push_back(std::move(obs));
    });
    auto protocol = std::make_shared<Protocol>();
    protocol->m_id = 6102;
    protocol->m_projectId = project.m_id;
    protocol->m_type = ProtocolType::kHttps;
    protocol->m_reqBodyType = ProtocolBodyType::kNone;
    protocol->m_respBodyType = ProtocolBodyType::kJson;
    protocol->m_reqCfg = {{"method", "GET"}, {"path", "/secure"}, {"headers", nlohmann::json::object()}};
    protocol->m_respCfg = {{"status_code", "200"}, {"headers", {{"Content-Type", "application/json"}}}};
    const std::string body = R"({"ok":true})";
    protocol->m_respBodyData.assign(body.begin(), body.end());
    auto item = ProtocolItemFactory::Create(protocol, server);
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(server->AddProtocolItem(item).ok());
    server->start();
    project.m_listenPort = server->getBindAddr().toPort();
    auto request = [&](const std::string& path, const std::string& expected) {
        TlsClient client(project.m_listenPort, certs.caFile());
        ASSERT_TRUE(client.connect());
        ASSERT_TRUE(client.write("GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"));
        const std::string response = client.read(4096);
        EXPECT_NE(response.find(expected), std::string::npos) << response;
        SSL_shutdown(client.ssl());
    };
    request("/secure", body);
    auto updated = protocol->m_reqCfg;
    updated["path"] = "/updated";
    ASSERT_TRUE(server->UpdateReqCfgProtocolItem(protocol->m_id, updated).ok());
    request("/secure", "404");
    request("/updated", body);
    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(observations.size(), 3);
        for(const auto& obs : observations) EXPECT_EQ(obs.protocol_type, ProtocolType::kHttps);
        EXPECT_EQ(observations[0].protocol_id, protocol->m_id);
        EXPECT_EQ(observations[1].scope, InteractionScope::kProject);
    }
    ASSERT_TRUE(server->stop());
    server.reset();
    lease = pool.acquire(project.m_id);
    ASSERT_TRUE(lease.ok());
    const auto saved_certificate = certs.certFile() + ".saved";
    std::filesystem::rename(certs.certFile(), saved_certificate);
    EXPECT_THROW(ProjectServerFactory::Create(project, lease.val), std::runtime_error);
    std::filesystem::rename(saved_certificate, certs.certFile());
    server = ProjectServerFactory::Create(project, lease.val);
    ASSERT_NE(server, nullptr);
    ASSERT_NE(std::dynamic_pointer_cast<HttpsProjectServer>(server), nullptr);
    EXPECT_EQ(server->getBindAddr().toPort(), project.m_listenPort);
    server->start();
    ASSERT_TRUE(server->stop());
}

} // namespace
