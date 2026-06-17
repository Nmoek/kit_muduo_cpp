/**
 * @file test_http.cpp
 * @brief
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-29 20:31:28
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "../test_log.h"
#include "net/http/http_request.h"
#include "net/http/http_context.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/http/http_response.h"
#include "net/http/http_server.h"
#include "net/event_loop.h"
#include "net/http/http_util.h"
#include "base/event_loop_thread.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <future>
#include <memory>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace {

struct FdGuard
{
    explicit FdGuard(int32_t input_fd = -1)
        :fd(input_fd)
    {}

    ~FdGuard()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
    }

    int32_t fd;
};

struct PickPortResult
{
    bool ok{false};
    uint16_t port{0};
    std::string error;
};

PickPortResult PickUnusedLoopbackPort()
{
    PickPortResult result;
    FdGuard listen_fd(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if(listen_fd.fd < 0)
    {
        result.error = "create socket failed: ";
        result.error += std::strerror(errno);
        return result;
    }

    int32_t on = 1;
    ::setsockopt(listen_fd.fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(::bind(listen_fd.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        result.error = "bind loopback failed: ";
        result.error += std::strerror(errno);
        return result;
    }

    socklen_t addr_len = sizeof(addr);
    if(::getsockname(listen_fd.fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0)
    {
        result.error = "getsockname failed: ";
        result.error += std::strerror(errno);
        return result;
    }

    result.ok = true;
    result.port = ::ntohs(addr.sin_port);
    return result;
}

int32_t ConnectLoopback(uint16_t port)
{
    for(int32_t i = 0; i < 50; ++i)
    {
        int32_t fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if(fd < 0)
        {
            return -1;
        }

        timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);

        if(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            return fd;
        }

        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return -1;
}

bool SendAll(int32_t fd, const std::string &data)
{
    const char *cur = data.data();
    size_t left = data.size();
    while(left > 0)
    {
        ssize_t n = ::send(fd, cur, left, 0);
        if(n < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return false;
        }

        cur += n;
        left -= static_cast<size_t>(n);
    }

    return true;
}

std::string ReadAll(int32_t fd)
{
    std::string data;
    char buf[4096];
    while(true)
    {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if(n > 0)
        {
            data.append(buf, static_cast<size_t>(n));
            continue;
        }
        if(n == 0)
        {
            break;
        }
        if(errno == EINTR)
        {
            continue;
        }
        break;
    }

    return data;
}

class HttpServerTestGuard
{
public:
    HttpServerTestGuard(EventLoop *loop, std::shared_ptr<HttpServer> *server)
        :loop_(loop)
        ,server_(server)
        ,cleaned_(false)
    {}

    ~HttpServerTestGuard()
    {
        cleanup();
    }

    void cleanup()
    {
        if(cleaned_ || loop_ == nullptr)
        {
            return;
        }

        auto stopped = std::make_shared<std::promise<void>>();
        auto stopped_future = stopped->get_future();
        loop_->runInLoop([this, stopped](){
            if(server_ && *server_)
            {
                (*server_)->stopAsync([stopped](){
                    stopped->set_value();
                });
            }
            else
            {
                stopped->set_value();
            }
        });
        stopped_future.wait_for(std::chrono::seconds(2));

        auto done = std::make_shared<std::promise<void>>();
        auto done_future = done->get_future();
        loop_->runInLoop([this, done](){
            if(server_)
            {
                server_->reset();
            }
            loop_->quit();
            done->set_value();
        });

        done_future.wait_for(std::chrono::seconds(2));
        cleaned_ = true;
    }

private:
    EventLoop *loop_;
    std::shared_ptr<HttpServer> *server_;
    bool cleaned_;
};

struct HttpBindJsonDto
{
    int32_t id{0};
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HttpBindJsonDto, id, name)
};

struct HttpBindMultipartDto
{
    HttpBindJsonDto header;
    std::vector<char> body;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HttpBindMultipartDto, header, body)
};

std::string MakeMultipartBody(const std::string &boundary,
                              const std::string &header_json,
                              const std::string &body_data)
{
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"header\"\r\n";
    oss << "\r\n";
    oss << header_json << "\r\n";
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"body\"\r\n";
    oss << "\r\n";
    oss << body_data << "\r\n";
    oss << "--" << boundary << "--\r\n";
    return oss.str();
}

HttpContextPtr MakeBindContext(const std::string &content_type, const std::string &body)
{
    auto ctx = std::make_shared<HttpContext>();
    auto req = ctx->request();
    req->setVersion(Version::kHttp11);
    req->setMethod(HttpRequest::Method::kPost);
    req->setPath("/bind-test");
    req->addHeader("Content-Type", content_type);
    Body req_body;
    req_body.appendData(body);
    req->setBody(req_body);
    return ctx;
}

} // namespace

namespace kit_muduo {

template<>
struct MultipartObjectBinder<HttpBindMultipartDto>
{
    static ContentCodecResult Bind(const MultiFormParser::PartMap &parts, HttpBindMultipartDto *out)
    {
        auto result = ParseJsonPartToObject(parts, "header", &out->header);
        if(!result.ok)
        {
            return result;
        }

        return ParseOctetStreamPartToRaw(parts, "body", out->body);
    }
};

} // namespace kit_muduo

static const char g_test_req[] = \
"GET /index.html HTTP/1.1\r\n" \
"Host: www.chenshuo.com\r\n" \
"User-Agent: kit_muduo\r\n" \
"Content-Length: 15\r\n" \
"Content-Type: text/plain\r\n" \
"Accept-Encoding: UTF-8\r\n" \
"\r\n561wefwe65f1ewf";

#if 0
static const char g_test_resp[] = \
"HTTP/1.1 200 OK\r\n" \
"Host: www.chenshuo.com\r\n" \
"Content-Length: 6\r\n" \
"Accept-Encoding: UTF-8\r\n" \
"\r\n123456";
#endif

TEST(TestHttpReq, raw_data)
{
    HttpContext context;
    Buffer buf;
    buf.append(g_test_req, strlen(g_test_req));
    EXPECT_EQ(buf.readableBytes(), strlen(g_test_req));

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req->method().toStr(), "GET");
    EXPECT_STREQ(req->path().c_str(), "/index.html");
    EXPECT_STREQ(req->version().toStr(), "HTTP/1.1");
    auto it = req->headers().find("Host");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Host");
    EXPECT_STREQ(it->second.c_str(), "www.chenshuo.com");

    it = req->headers().find("User-Agent");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "User-Agent");
    EXPECT_STREQ(it->second.c_str(), "kit_muduo");

    it = req->headers().find("Accept-Encoding");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Accept-Encoding");
    EXPECT_STREQ(it->second.c_str(), "UTF-8");

    it = req->headers().find("Content-Length");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Content-Length");
    EXPECT_STREQ(it->second.c_str(), "15");

    EXPECT_EQ(now.millSeconds(), req->receiveTime().millSeconds());

    TEST_INFO() << "Body: "<< "|" << req->body().toString() << "|" << std::endl;
}

/*
测试思路：
1. HttpContext::bindJson 是 Web 层 facade，只允许 application/json 请求体。
2. 请求头带 charset 参数时，base Content-Type resolver 仍应识别为 JSON。
3. 绑定成功后 DTO 字段被填充，证明 facade 正确把 HttpRequest 包装成 ContentView。

示例：
  Content-Type=application/json; charset=utf-8
  body={"id":11,"name":"ctx-json"}
        |
        v
  bindJson ok
*/
TEST(TestHttpContextBind, BindJsonAcceptsJsonWithCharset)
{
    auto ctx = MakeBindContext(
        "application/json; charset=utf-8",
        R"({"id":11,"name":"ctx-json"})");
    HttpBindJsonDto dto;

    const auto result = ctx->bindJson(&dto);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.id, 11);
    EXPECT_EQ(dto.name, "ctx-json");
}

/*
测试思路：
1. bindJson 的接口契约是 JSON-only。
2. 请求 Content-Type 为 text/plain，即使 body 内容是合法 JSON，也必须拒绝。
3. 返回 kUnsupportedFormat，handler 可以统一转成 body parse error。

示例：
  Content-Type=text/plain, body={"id":11}
        |
        v
  bindJson failed
*/
TEST(TestHttpContextBind, BindJsonRejectsPlainText)
{
    auto ctx = MakeBindContext(
        "text/plain",
        R"({"id":11,"name":"ctx-json"})");
    HttpBindJsonDto dto;

    const auto result = ctx->bindJson(&dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

/*
测试思路：
1. bindJson 不能接受 multipart/form-data，否则 JSON-only handler 的契约会变得模糊。
2. 构造一个合法 multipart 请求体，但调用 bindJson。
3. facade 应在 allowed_formats 检查阶段返回 kUnsupportedFormat。

示例：
  multipart/form-data + bindJson
        |
        v
  kUnsupportedFormat
*/
TEST(TestHttpContextBind, BindJsonRejectsMultipart)
{
    const std::string boundary = "CTX-MULTIPART-JSON-REJECT";
    auto ctx = MakeBindContext(
        "multipart/form-data; boundary=" + boundary,
        MakeMultipartBody(boundary, R"({"id":12,"name":"ctx-multipart"})", "abc"));
    HttpBindJsonDto dto;

    const auto result = ctx->bindJson(&dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

/*
测试思路：
1. bindMultipart 是 multipart-only facade。
2. 构造 header JSON part 和 body 原始字节 part，DTO 有对应 MultipartObjectBinder 特化。
3. 绑定成功后，header 对象和 body 字节都应正确写入 DTO。

示例：
  multipart:
    header={"id":13,"name":"ctx-multipart"}
    body="payload"
        |
        v
  bindMultipart ok
*/
TEST(TestHttpContextBind, BindMultipartAcceptsMultipartWithBoundary)
{
    const std::string boundary = "CTX-MULTIPART-OK";
    auto ctx = MakeBindContext(
        "multipart/form-data; boundary=" + boundary,
        MakeMultipartBody(boundary, R"({"id":13,"name":"ctx-multipart"})", "payload"));
    HttpBindMultipartDto dto;

    const auto result = ctx->bindMultipart(&dto);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(dto.header.id, 13);
    EXPECT_EQ(dto.header.name, "ctx-multipart");
    EXPECT_EQ(std::string(dto.body.begin(), dto.body.end()), "payload");
}

/*
测试思路：
1. bindMultipart 不能接受 JSON 请求体。
2. 该行为保证 AddProtocol/DetailBody 这类 multipart-only handler 不会误收 JSON body。
3. facade 应直接返回 kUnsupportedFormat，不进入 MultipartObjectBinder。

示例：
  Content-Type=application/json + bindMultipart
        |
        v
  kUnsupportedFormat
*/
TEST(TestHttpContextBind, BindMultipartRejectsJson)
{
    auto ctx = MakeBindContext(
        "application/json",
        R"({"id":13,"name":"ctx-json"})");
    HttpBindMultipartDto dto;

    const auto result = ctx->bindMultipart(&dto);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ContentCodecErrorCode::kUnsupportedFormat);
}

TEST(TestHttpReq, query_params)
{
    static const char test_req[] = \
    "GET /projects?project_id=42&name=kit+muduo&empty=&encoded=a%2Bb%20c&flag HTTP/1.1\r\n" \
    "Host: localhost\r\n" \
    "\r\n";

    HttpContext context;
    Buffer buf;
    buf.append(test_req, strlen(test_req));

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req->path().c_str(), "/projects");
    EXPECT_STREQ(req->getQureyParam("project_id").c_str(), "42");
    EXPECT_STREQ(req->getQureyParam("name").c_str(), "kit muduo");
    EXPECT_STREQ(req->getQureyParam("empty").c_str(), "");
    EXPECT_STREQ(req->getQureyParam("encoded").c_str(), "a+b c");
    EXPECT_STREQ(req->getQureyParam("flag").c_str(), "");
}

TEST(TestHttpReq, query_params_segmented_url)
{
    HttpContext context;
    auto now = TimeStamp::Now();

    EXPECT_EQ(context.parseRequest("GET /pro", now), true);
    EXPECT_EQ(context.gotAll(), false);

    bool ok = context.parseRequest("jects?project_id=42&name=kit+muduo HTTP/1.1\r\n"
                                   "Host: localhost\r\n"
                                   "\r\n", now);

    EXPECT_EQ(ok, true);
    EXPECT_EQ(context.gotAll(), true);
    auto req = context.request();
    EXPECT_STREQ(req->path().c_str(), "/projects");
    EXPECT_STREQ(req->getQureyParam("project_id").c_str(), "42");
    EXPECT_STREQ(req->getQureyParam("name").c_str(), "kit muduo");
}

TEST(TestHttpReq, buffer_partial_body_keeps_parser_state)
{
    static const char first_part[] =
    "POST /partial HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Content-Length: 5\r\n"
    "Content-Type: text/plain\r\n"
    "\r\nhe";
    static const char second_part[] = "llo";

    HttpContext context;
    Buffer buf;
    auto now = TimeStamp::Now();

    buf.append(first_part, strlen(first_part));
    EXPECT_EQ(context.parseRequest(buf, now), true);
    EXPECT_EQ(context.gotAll(), false);
    EXPECT_EQ(buf.readableBytes(), 0);
    EXPECT_STREQ(context.request()->body().toString().c_str(), "he");

    buf.append(second_part, strlen(second_part));
    EXPECT_EQ(context.parseRequest(buf, now), true);
    EXPECT_EQ(context.gotAll(), true);
    EXPECT_EQ(buf.readableBytes(), 0);

    auto req = context.request();
    EXPECT_STREQ(req->method().toStr(), "POST");
    EXPECT_STREQ(req->path().c_str(), "/partial");
    EXPECT_STREQ(req->body().toString().c_str(), "hello");
}

TEST(TestHttpReq, buffer_pipelining_leaves_next_request_readable)
{
    const std::string first_req =
    "GET /one HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "\r\n";
    const std::string second_req =
    "GET /two HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "\r\n";

    Buffer buf;
    buf.append(first_req.data(), first_req.size());
    buf.append(second_req.data(), second_req.size());

    auto now = TimeStamp::Now();
    HttpContext first_context;
    EXPECT_EQ(first_context.parseRequest(buf, now), true);
    EXPECT_EQ(first_context.gotAll(), true);
    EXPECT_STREQ(first_context.request()->path().c_str(), "/one");
    EXPECT_EQ(buf.readableBytes(), second_req.size());
    EXPECT_EQ(buf.lookAllAsString(), second_req);

    HttpContext second_context;
    EXPECT_EQ(second_context.parseRequest(buf, now), true);
    EXPECT_EQ(second_context.gotAll(), true);
    EXPECT_STREQ(second_context.request()->path().c_str(), "/two");
    EXPECT_EQ(buf.readableBytes(), 0);
}


TEST(TestHttpReq, create_data)
{
    HttpRequest orireq;

    orireq.setMethod(HttpRequest::Method::kGet);
    orireq.setPath("/main.html");
    orireq.setVersion(Version::kHttp11);
    orireq.addHeader("Host", "www.kit.com");
    orireq.addHeader("XData", "666");
    std::string body = "12345678";
    orireq.body().appendData(body);
    orireq.body().setContentType(ContentType::kPlainType);
    orireq.addHeader("Content-Length", std::to_string(body.size()));


    std::cout << orireq.toString() << std::endl;
    std::cout << "---------------------\n";

    Buffer buf;
    buf.append(orireq.toString().c_str(), orireq.toString().size());
    EXPECT_EQ(buf.readableBytes(), orireq.toString().size());

    HttpContext context;

    auto now = TimeStamp::Now();
    bool ok = context.parseRequest(buf, now);

    EXPECT_EQ(ok, true);
    auto req = context.request();
    EXPECT_STREQ(req->method().toStr(), "GET");
    EXPECT_STREQ(req->path().c_str(), "/main.html");
    EXPECT_STREQ(req->version().toStr(), "HTTP/1.1");
    auto it = req->headers().find("Host");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "Host");
    EXPECT_STREQ(it->second.c_str(), "www.kit.com");

    it = req->headers().find("XData");
    EXPECT_TRUE(it != req->headers().end());
    EXPECT_STREQ(it->first.c_str(), "XData");
    EXPECT_STREQ(it->second.c_str(), "666");

    EXPECT_EQ(now.millSeconds(), req->receiveTime().millSeconds());
    EXPECT_STREQ(req->body().toString().c_str(), "12345678");

    TEST_INFO() << "Body: "<< "|" << req->body().toString() << "|" << std::endl;
}


void testHttpCb(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    TEST_INFO() << "req body= " << req->body().toString() << std::endl;
    resp->setStateCode(StateCode::k200Ok);
    resp->setVersion(Version::kHttp11);
    resp->setConnectionClosed(true);

    resp->body().appendData(req->body().data());
    resp->body().appendData("\n");
    conn->send(resp->toString());

}

TEST(TestHttpResp, create_data)
{
    HttpResponse oriresp;
    oriresp.setVersion(Version::kHttp11);
    oriresp.setStateCode(StateCode::k200Ok);
    oriresp.addHeader("XData", "999");
    oriresp.body().appendData("123456", strlen("123456"));
    oriresp.body().setContentType(ContentType::kPlainType);
    std::cout << oriresp.toString() << std::endl;
    std::cout << "----------------\n";

    HttpContext context;

    auto now = TimeStamp::Now();
    bool ok = context.parseResponse(oriresp.toString(), now);

    EXPECT_EQ(ok, true);
    auto resp = context.response();
    EXPECT_STREQ(resp->stateCode().toString().c_str(), "200");
    EXPECT_STREQ(resp->version().toStr(), "HTTP/1.1");
    auto it = resp->headers().find("XData");
    EXPECT_TRUE(it != resp->headers().end());
    EXPECT_STREQ(it->first.c_str(), "XData");
    EXPECT_STREQ(it->second.c_str(), "999");

    EXPECT_STREQ(resp->body().toString().c_str(), "123456");

    TEST_INFO() << "Body: "<< "|" << resp->body().toString() << "|" << std::endl;


}

TEST(TestHttpResp, body_without_content_type_defaults_to_octet_stream)
{
    static const char test_resp[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 6\r\n"
    "Connection: close\r\n"
    "\r\n"
    "abcdef";

    HttpContext context;
    Buffer buf;
    buf.append(test_resp, strlen(test_resp));

    auto now = TimeStamp::Now();
    EXPECT_EQ(context.parseResponse(buf, now), true);
    EXPECT_EQ(context.gotAll(), true);
    EXPECT_EQ(buf.readableBytes(), 0);

    auto resp = context.response();
    EXPECT_STREQ(resp->stateCode().toString().c_str(), "200");
    EXPECT_STREQ(resp->body().toString().c_str(), "abcdef");
    EXPECT_EQ(resp->body().contentType()(), ContentType::kOctetStream);
}

TEST(TestHttpResp, binary_body_without_content_type_preserves_bytes)
{
    static const char header[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n";
    const std::string body("a\0b\0c", 5);

    Buffer buf;
    buf.append(header, strlen(header));
    buf.append(body.data(), body.size());

    HttpContext context;
    auto now = TimeStamp::Now();
    EXPECT_EQ(context.parseResponse(buf, now), true);
    EXPECT_EQ(context.gotAll(), true);
    EXPECT_EQ(buf.readableBytes(), 0);

    auto resp = context.response();
    EXPECT_EQ(resp->body().toString(), body);
    EXPECT_EQ(resp->body().data().size(), body.size());
    EXPECT_EQ(resp->body().contentType()(), ContentType::kOctetStream);
}

TEST(TestHttpServer, pipelined_requests_are_dispatched_separately)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "http_pipeline_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<HttpServer> server;
    HttpServerTestGuard guard(loop, &server);
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&](){
        InetAddress addr(port, "127.0.0.1");
        server = std::make_shared<HttpServer>(loop, addr, "http-pipeline-test", false, TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setHttpCallback([](TcpConnectionPtr conn, HttpContextPtr ctx) {
            auto req = ctx->request();
            auto resp = ctx->response();
            resp->setVersion(Version::kHttp11);
            resp->setStateCode(StateCode::k200Ok);
            resp->body().appendData(req->path());
            if(req->path() == "/two")
            {
                resp->setConnectionClosed(true);
            }
            conn->send(resp->toString());
            if(resp->connectionClosed())
            {
                conn->shutdown();
            }
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);

    const std::string requests =
        "GET /one HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "\r\n"
        "GET /two HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_TRUE(SendAll(client_fd.fd, requests));

    const std::string response = ReadAll(client_fd.fd);
    const size_t first_resp = response.find("HTTP/1.1 200 OK\r\n");
    ASSERT_NE(first_resp, std::string::npos) << response;

    const size_t first_body = response.find("\r\n\r\n/one", first_resp);
    ASSERT_NE(first_body, std::string::npos) << response;

    const size_t second_resp = response.find("HTTP/1.1 200 OK\r\n", first_resp + 1);
    ASSERT_NE(second_resp, std::string::npos) << response;

    const size_t second_body = response.find("\r\n\r\n/two", second_resp);
    ASSERT_NE(second_body, std::string::npos) << response;

    guard.cleanup();
}

TEST(TestHttpServer, BusinessThreadPoolSubmitFailureReturns503)
{
    auto port_result = PickUnusedLoopbackPort();
    if(!port_result.ok)
    {
        GTEST_SKIP() << "loopback TCP socket unavailable: " << port_result.error;
    }
    const uint16_t port = port_result.port;

    EventLoopThread loop_thread(nullptr, "http_submit_failure_test");
    EventLoop *loop = loop_thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::shared_ptr<HttpServer> server;
    HttpServerTestGuard guard(loop, &server);
    std::promise<void> started;
    auto started_future = started.get_future();

    loop->runInLoop([&](){
        InetAddress addr(port, "127.0.0.1");
        server = std::make_shared<HttpServer>(loop, addr, "http-submit-failure-test", true, TcpServer::KReusePort);
        server->setThreadNum(0);
        server->setBusinessThreadPoolConfig(HttpServer::BusinessThreadPoolConfig{
            1,
            0,
            2,
            10
        });
        server->Get("/slow", [](TcpConnectionPtr conn, HttpContextPtr ctx) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto resp = ctx->response();
            resp->setVersion(Version::kHttp11);
            resp->setStateCode(StateCode::k200Ok);
            resp->setConnectionClosed(true);
            resp->body().appendData("ok");
        });
        server->start();
        started.set_value();
    });

    ASSERT_EQ(started_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    FdGuard client_fd(ConnectLoopback(port));
    ASSERT_GE(client_fd.fd, 0);

    const std::string request =
        "GET /slow HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_TRUE(SendAll(client_fd.fd, request));

    const std::string response = ReadAll(client_fd.fd);
    ASSERT_NE(response.find("HTTP/1.1 503 Service Unavailable\r\n"), std::string::npos)
        << response;
    ASSERT_NE(response.find("Connection: close\r\n"), std::string::npos)
        << response;

    guard.cleanup();
}

TEST(TestHttpServer, DISABLED_listen)
{
    EventLoop loop;
    InetAddress addr(5555, "192.168.77.136");
    HttpServer server(&loop, addr, "myhttp", TcpServer::KReusePort);
    server.setThreadNum(1);
    server.setHttpCallback(testHttpCb);

    server.start();
    loop.loop();
}


TEST(TestHttpServer, DISABLED_servlet)
{
    EventLoop loop;
    InetAddress addr(5555, "192.168.77.136");
    HttpServer server(&loop, addr, "http-servlet", TcpServer::KReusePort);
    server.setThreadNum(1);

    auto sd = server.getServletDispatch();

    // 添加固定的服务
    sd->addRoute(ExpectHttpMethods::Get, "/hello", std::make_shared<HelloServlet>("kit_proxy_server/1.0.0"));

    // 自定义函数形式
    sd->addRoute(ExpectHttpMethods::Get, "/custom", [](TcpConnectionPtr conn,  HttpContextPtr ctx) {

        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);
        resp->setStateCode(StateCode::k200Ok);
        resp->addHeader("Content-Type", "text/plain");

        std::string body = "this is a custom servlet!!";
        resp->body().appendData(body);
    });

    server.setHttpCallback([dispatch = sd](TcpConnectionPtr conn, HttpContextPtr ctx){
        auto req = ctx->request();
        auto resp = ctx->response();

        const std::string &connection = req->getHeader("Connection");
        bool closed = (connection == "close")
                || (Version::kHttp10 == req->version()() && connection != "keep-alive");


        resp->setConnectionClosed(closed);

        ////////////这部分可以异步//////////////
        dispatch->handle(conn, ctx);

        conn->send(req->toString());
        if(resp->connectionClosed())
        {
            conn->shutdown();
        }
        ////////////这部分可以异步//////////////
    });

    server.start();
    loop.loop();
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
