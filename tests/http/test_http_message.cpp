/**
 * @file test_http_message.cpp
 * @brief HTTP request/response parser 与报文模型测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/http/http_context.h"
#include "net/http/http_content.h"
#include "net/http/http_parser.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace kit_muduo;
using namespace kit_muduo::http;

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

/*
测试思路：
1. 用完整 HTTP request 原始字节喂给 HttpContext。
2. 验证 method/path/version/header/body 都从报文中解析出来。
3. receiveTime 应保存调用 parseRequest 时传入的时间戳。

示例：
  GET /index.html + Content-Length:15 + body
        |
        v
  request.path == "/index.html", body == "561wefwe65f1ewf"
*/
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

    TEST_INFO() << "Body: "<< "|" << req->bodyString() << "|" << std::endl;
}


/*
测试思路：
1. URL query 中同时覆盖普通参数、加号空格、百分号编码、空值和无等号 flag。
2. HttpRequest 应把 path 与 query 参数拆开。
3. getQureyParam 返回解码后的参数值。

示例：
  /projects?project_id=42&name=kit+muduo&encoded=a%2Bb%20c&flag
        |
        v
  path=/projects, name="kit muduo", encoded="a+b c", flag=""
*/
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

/*
测试思路：
1. 模拟 TCP 分段导致 URL 被拆成两次 parseRequest 输入。
2. 第一次未收完整请求时 gotAll 应为 false。
3. 第二次补齐后 query 参数仍应正确解析。

示例：
  "GET /pro" + "jects?project_id=42..."
        |
        v
  path=/projects, project_id=42
*/
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

/*
测试思路：
1. 模拟 header 和部分 body 同批到达，剩余 body 后续到达。
2. parser 应保存已读 body 和 Content-Length 状态。
3. body 补齐后 gotAll 为 true，Buffer 没有残留。

示例：
  body: "he" + "llo"
        |
        v
  request.body == "hello"
*/
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
    EXPECT_STREQ(context.request()->bodyString().c_str(), "he");

    buf.append(second_part, strlen(second_part));
    EXPECT_EQ(context.parseRequest(buf, now), true);
    EXPECT_EQ(context.gotAll(), true);
    EXPECT_EQ(buf.readableBytes(), 0);

    auto req = context.request();
    EXPECT_STREQ(req->method().toStr(), "POST");
    EXPECT_STREQ(req->path().c_str(), "/partial");
    EXPECT_STREQ(req->bodyString().c_str(), "hello");
}

/*
测试思路：
1. 一个 Buffer 中连续放入两个 HTTP request。
2. 第一次解析只消费第一条完整请求。
3. 第二条请求应留在 Buffer 中，供下一个 HttpContext 继续解析。

示例：
  GET /one\r\n\r\nGET /two\r\n\r\n
        |
        v
  first.path=/one, buffer 剩余 /two 请求
*/
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

/*
测试思路：
1. HTTP header 名称大小写不敏感，parser 收到 content-type 小写时也应同步 ContentMeta。
2. getHeader("Content-Type") 也必须能取到原始 header 值。
3. 该用例覆盖 setHeaders -> ParseHttpContentType 的统一同步入口。

示例：
  content-type: application/json; charset=utf-8
        |
        v
  request.contentMeta.known_type == kApplicationJson
*/
TEST(TestHttpReq, content_type_header_is_case_insensitive)
{
    static const char test_req[] =
    "POST /json HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "content-type: application/json; charset=utf-8\r\n"
    "Content-Length: 2\r\n"
    "\r\n{}";

    HttpContext context;
    Buffer buf;
    buf.append(test_req, strlen(test_req));

    auto now = TimeStamp::Now();
    EXPECT_EQ(context.parseRequest(buf, now), true);
    EXPECT_EQ(context.gotAll(), true);

    auto req = context.request();
    EXPECT_EQ(req->getHeader("Content-Type"), "application/json; charset=utf-8");
    EXPECT_EQ(req->contentMeta().known_type, KnownMediaType::kApplicationJson);
    EXPECT_EQ(ResolveContentCodecFormat(req->contentMeta()), ContentCodecFormat::kJson);
    ASSERT_TRUE(req->contentMeta().params.count("charset"));
    EXPECT_EQ(req->contentMeta().params.at("charset"), "utf-8");
}


/*
测试思路：
1. 先通过 HttpRequest::toString 生成 HTTP request 报文。
2. 再把生成的报文回喂给 parser。
3. 验证序列化和反序列化在 method/path/header/body 上闭环。

示例：
  HttpRequest(url="/main.html", body="12345678") -> toString -> parseRequest
        |
        v
  parsed body == "12345678"
*/
TEST(TestHttpReq, create_data)
{
    HttpRequest orireq;

    orireq.setMethod(HttpRequest::Method::kGet);
    orireq.setUrl("/main.html");
    orireq.setPath("/main.html");
    orireq.setVersion(Version::kHttp11);
    orireq.addHeader("Host", "www.kit.com");
    orireq.addHeader("XData", "666");
    std::string body = "12345678";
    orireq.appendBodyData(body);
    orireq.setContentMeta(MakeContentMeta(KnownMediaType::kTextPlain));
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
    EXPECT_STREQ(req->bodyString().c_str(), "12345678");

    TEST_INFO() << "Body: "<< "|" << req->bodyString() << "|" << std::endl;
}



/*
测试思路：
1. 先通过 HttpResponse::toString 生成 HTTP response 报文。
2. 再把生成的报文回喂给 response parser。
3. 验证 status/version/header/body 序列化闭环。

示例：
  HttpResponse(200, body="123456") -> toString -> parseResponse
        |
        v
  parsed status=200, body="123456"
*/
TEST(TestHttpResp, create_data)
{
    HttpResponse oriresp;
    oriresp.setVersion(Version::kHttp11);
    oriresp.setStateCode(StateCode::k200Ok);
    oriresp.addHeader("XData", "999");
    oriresp.appendBodyData("123456", strlen("123456"));
    oriresp.setContentMeta(MakeContentMeta(KnownMediaType::kTextPlain));
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

    EXPECT_STREQ(resp->bodyString().c_str(), "123456");

    TEST_INFO() << "Body: "<< "|" << resp->bodyString() << "|" << std::endl;


}

/*
测试思路：
1. HTTP response 有 body 但没有 Content-Type。
2. HttpResponse 不应把缺失类型猜成 text/json/octet-stream。
3. ContentMeta 保持 unknown，body bytes 仍完整保留。

示例：
  HTTP/1.1 200 OK + Content-Length:6 + no Content-Type
        |
        v
  contentMeta.known_type == kUnknown
*/
TEST(TestHttpResp, body_without_content_type_keeps_unknown_content_meta)
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
    EXPECT_STREQ(resp->bodyString().c_str(), "abcdef");
    EXPECT_EQ(resp->contentMeta().known_type, KnownMediaType::kUnknown);
    EXPECT_TRUE(resp->contentMeta().media_type.empty());
}

/*
测试思路：
1. response body 未声明 Content-Type，但内容包含 NUL 字节。
2. parser 不能按 C 字符串截断 body。
3. bodyData 和 bodyString 都应保留完整 5 字节。

示例：
  bytes={'a','\0','b','\0','c'}
        |
        v
  body.size == 5, contentMeta.known_type == kUnknown
*/
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
    EXPECT_EQ(resp->bodyString(), body);
    EXPECT_EQ(resp->bodyData().size(), body.size());
    EXPECT_EQ(resp->contentMeta().known_type, KnownMediaType::kUnknown);
}

/*
测试思路：
1. HttpResponse::toBytes 是真实发送路径使用的二进制安全序列化接口。
2. body 含 NUL 字节时，序列化结果必须包含完整 body，不能按 C 字符串截断。
3. 回喂 response parser 后 bodyData 应与原始 vector<uint8_t> 完全一致。

示例：
  body={'a','\0','b','\0','c'} -> toBytes -> parseResponse -> body.size()==5
*/
TEST(TestHttpResp, to_bytes_preserves_binary_body)
{
    const std::vector<uint8_t> body{
        static_cast<uint8_t>('a'),
        static_cast<uint8_t>('\0'),
        static_cast<uint8_t>('b'),
        static_cast<uint8_t>('\0'),
        static_cast<uint8_t>('c'),
    };

    HttpResponse oriresp;
    oriresp.setVersion(Version::kHttp11);
    oriresp.setStateCode(StateCode::k200Ok);
    oriresp.setContentMeta(MakeContentMeta(KnownMediaType::kApplicationOctetStream));
    oriresp.setBodyData(body);

    const auto bytes = oriresp.toBytes();
    ASSERT_GE(bytes.size(), body.size());
    EXPECT_EQ(std::vector<uint8_t>(bytes.end() - body.size(), bytes.end()), body);

    Buffer buf;
    buf.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    HttpContext context;
    auto now = TimeStamp::Now();
    EXPECT_EQ(context.parseResponse(buf, now), true);
    EXPECT_EQ(context.gotAll(), true);

    auto resp = context.response();
    EXPECT_EQ(resp->contentMeta().known_type, KnownMediaType::kApplicationOctetStream);
    EXPECT_EQ(resp->bodyData(), body);
}

/*
测试思路：
1. HttpResponse::setJson 是业务 handler 写标准 JSON 响应的统一入口。
2. 调用后应覆盖 body，并同步 Content-Type 到 application/json。
3. toBytes 序列化时仍应自动补齐 Content-Length。

示例：
  setJson({"code":0}) -> Content-Type=application/json, body={"code":0}
*/
TEST(TestHttpResp, set_json_writes_body_and_content_meta)
{
    HttpResponse resp;
    resp.setVersion(Version::kHttp11);
    resp.setStateCode(StateCode::k200Ok);

    resp.setJson(nlohmann::json{{"code", 0}, {"message", "success"}});

    EXPECT_EQ(resp.contentMeta().known_type, KnownMediaType::kApplicationJson);
    EXPECT_EQ(resp.getHeader("Content-Type"), "application/json");
    EXPECT_EQ(nlohmann::json::parse(resp.bodyString()),
              (nlohmann::json{{"code", 0}, {"message", "success"}}));
    const auto data = resp.toString();
    EXPECT_NE(data.find("Content-Length: "), std::string::npos);
}

/*
测试思路：
1. HttpResponse::setText 用于普通文本响应，不应复用 JSON 类型。
2. 调用后 body 是原始字符串，Content-Type 是 text/plain。
3. toBytes 负责在文本类型上追加 charset。

示例：
  setText("ok") -> Content-Type=text/plain; charset=utf-8, body=ok
*/
TEST(TestHttpResp, set_text_writes_plain_text)
{
    HttpResponse resp;
    resp.setVersion(Version::kHttp11);
    resp.setStateCode(StateCode::k200Ok);

    resp.setText("ok");

    EXPECT_EQ(resp.contentMeta().known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(resp.getHeader("Content-Type"), "text/plain");
    EXPECT_EQ(resp.bodyString(), "ok");
    const auto data = resp.toString();
    EXPECT_NE(data.find("Content-Type: text/plain; charset=utf-8"), std::string::npos);
}

/*
测试思路：
1. HttpResponse::setOctetStream 用于二进制响应，body 中的 NUL 字节不能丢失。
2. 调用后 Content-Type 是 application/octet-stream。
3. bodyData 与输入 bytes 完全一致。

示例：
  bytes={'a','\0','b'} -> setOctetStream -> bodyData.size()==3
*/
TEST(TestHttpResp, set_octet_stream_preserves_binary_body)
{
    std::vector<uint8_t> body{
        static_cast<uint8_t>('a'),
        static_cast<uint8_t>('\0'),
        static_cast<uint8_t>('b'),
    };

    HttpResponse resp;
    resp.setVersion(Version::kHttp11);
    resp.setStateCode(StateCode::k200Ok);
    resp.setOctetStream(body);

    EXPECT_EQ(resp.contentMeta().known_type, KnownMediaType::kApplicationOctetStream);
    EXPECT_EQ(resp.getHeader("Content-Type"), "application/octet-stream");
    EXPECT_EQ(resp.bodyData(), body);
}

/*
测试思路：
1. 直接验证 TryConsume 的边界条件，确保达到上限合法，超过上限拒绝。
2. 该函数同时被 CustomHttpParser 和 LLhttpParser 使用，是所有累计限制的基础。

示例：
  current=4, input=1, limit=5 -> current=5, success
  current=5, input=1, limit=5 -> reject
*/
TEST(TestHttpParserLimits, try_consume_enforces_limit_without_overflow)
{
    size_t current = 4;
    EXPECT_TRUE(HttpParser::TryConsume(current, 1, 5));
    EXPECT_EQ(current, 5U);
    EXPECT_FALSE(HttpParser::TryConsume(current, 1, 5));
    EXPECT_EQ(current, 5U);

    current = std::numeric_limits<size_t>::max();
    EXPECT_FALSE(HttpParser::TryConsume(current, 1, current));
}

/*
测试思路：
1. 给 CustomHttpParser 一个很小的首行上限。
2. 请求行在已有 CRLF 的情况下也必须被拒绝，不能只限制“尚未找到 CRLF”的分片。
3. 错误原始数据捕获还必须受 max_error_capture_bytes 限制。

示例：
  max_start_line_bytes=16, "GET /too-long HTTP/1.1\\r\\n"
        |
        v
  kStartLineTooLarge, raw_capture.size() <= 4
*/
TEST(TestHttpParserLimits, custom_parser_rejects_oversized_start_line)
{
    HttpParseLimits limits;
    limits.max_start_line_bytes = 16;
    limits.max_error_capture_bytes = 4;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string request = "GET /too-long HTTP/1.1\r\n";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(parser.parse(buf));
    EXPECT_EQ(context.parseError(), HttpParseError::kStartLineTooLarge);
    EXPECT_LE(context.rawCapture().size(), limits.max_error_capture_bytes);
}

/*
测试思路：
1. 用累计 Header 字节上限覆盖“单个 Header 行超限”的路径。
2. 当前限制统计 Header 名称和值的有效内容字节，不包含冒号、空白和 CRLF。

示例：
  max_header_bytes=6, "X: 123456\\r\\n" (head=1, val=6)
        |
        v
  kHeadersTooLarge
*/
TEST(TestHttpParserLimits, custom_parser_rejects_oversized_headers)
{
    HttpParseLimits limits;
    limits.max_header_bytes = 6;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "X: 123456\r\n"
        "\r\n";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(parser.parse(buf));
    EXPECT_EQ(context.parseError(), HttpParseError::kHeadersTooLarge);
}

/*
测试思路：
1. 同一条未完成 Header 在两次 parse 之间会继续留在 Buffer 中。
2. pending Header 防护应按当前未完成原始片段的实际长度判断，不能把旧片段在下一次 parse 中重复累加。
3. 有效内容总长度未超过限制时，无论 TCP 如何分片都应该成功。

示例：
  "X: 123" + "456\\r\\n" -> head=1, val=6, total=7 <= 16
        |
        v
  first parse: wait, second parse: gotAll
*/
TEST(TestHttpParserLimits, custom_parser_does_not_double_count_fragmented_header)
{
    HttpParseLimits limits;
    limits.max_header_bytes = 16;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string first_part =
        "GET / HTTP/1.1\r\n"
        "X: 123";
    buf.append(first_part.data(), first_part.size());

    EXPECT_TRUE(parser.parse(buf));
    EXPECT_FALSE(context.gotAll());

    const std::string second_part = "456\r\n\r\n";
    buf.append(second_part.data(), second_part.size());
    EXPECT_TRUE(parser.parse(buf));
    EXPECT_TRUE(context.gotAll());
    EXPECT_EQ(context.request()->getHeader("X"), "123456");
}

/*
测试思路：
1. Header 没有 CRLF 时，parser 也必须对当前未完成片段执行上限检查。
2. 该检查用于阻止攻击者持续发送永不结束的超长 Header 行。

示例：
  max_header_bytes=8, 未结束的 "X: 123456" 原始片段为 9 bytes
        |
        v
  kHeadersTooLarge
*/
TEST(TestHttpParserLimits, custom_parser_rejects_oversized_incomplete_header)
{
    HttpParseLimits limits;
    limits.max_header_bytes = 8;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "X: 123456";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(parser.parse(buf));
    EXPECT_EQ(context.parseError(), HttpParseError::kHeadersTooLarge);
}

/*
测试思路：
1. 限制 Header 数量为 1。
2. 第一个 Header 合法，第二个 Header 必须在写入请求前被拒绝。

示例：
  max_header_count=1, Host + X-Trace
        |
        v
  kHeadersTooMany
*/
TEST(TestHttpParserLimits, custom_parser_rejects_too_many_headers)
{
    HttpParseLimits limits;
    limits.max_header_count = 1;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "X-Trace: 1\r\n"
        "\r\n";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(parser.parse(buf));
    EXPECT_EQ(context.parseError(), HttpParseError::kHeadersTooMany);
    EXPECT_EQ(context.request()->getHeader("Host"), "localhost");
    EXPECT_EQ(context.request()->getHeader("X-Trace"), "");
}

/*
测试思路：
1. Content-Length 声明值超过 max_body_bytes 时，在 Body 追加前直接拒绝。
2. 即使输入中已经带有完整 Body，也不能先分配/保存超限内容再报错。

示例：
  max_body_bytes=4, Content-Length: 5 + "12345"
        |
        v
  kBodyTooLarge, body.size()==0
*/
TEST(TestHttpParserLimits, custom_parser_rejects_oversized_declared_body_before_append)
{
    HttpParseLimits limits;
    limits.max_body_bytes = 4;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string request =
        "POST / HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "12345";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(parser.parse(buf));
    EXPECT_EQ(context.parseError(), HttpParseError::kBodyTooLarge);
    EXPECT_TRUE(context.request()->bodyData().empty());
}

/*
测试思路：
1. Content-Length 是 framing 字段，必须严格按十进制解析。
2. 非法字符不能被 atoi/宽松转换截断成合法长度。

示例：
  Content-Length: 1x -> kInvalidFormat
*/
TEST(TestHttpParserLimits, custom_parser_rejects_malformed_content_length)
{
    HttpContext context;
    CustomHttpParser parser(&context);
    Buffer buf;
    const std::string request =
        "POST / HTTP/1.1\r\n"
        "Content-Length: 1x\r\n"
        "\r\n"
        "1";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(parser.parse(buf));
    EXPECT_EQ(context.parseError(), HttpParseError::kInvalidFormat);
    EXPECT_TRUE(context.request()->bodyData().empty());
}

/*
测试思路：
1. 重复 Content-Length 即使数值相同也拒绝，避免多个 framing 来源产生歧义。
2. Transfer-Encoding 当前没有 chunked framing 实现，因此必须显式返回“不支持”，不能把 chunk 字节当普通 Body。

示例：
  Content-Length: 1 + Content-Length: 1 -> kInvalidFormat
  Transfer-Encoding: chunked -> kUnsupportedTransferEncoding
*/
TEST(TestHttpParserLimits, custom_parser_rejects_ambiguous_framing_headers)
{
    {
        HttpContext context;
        CustomHttpParser parser(&context);
        Buffer buf;
        const std::string request =
            "POST / HTTP/1.1\r\n"
            "Content-Length: 1\r\n"
            "Content-Length: 1\r\n"
            "\r\n"
            "1";
        buf.append(request.data(), request.size());

        EXPECT_FALSE(parser.parse(buf));
        EXPECT_EQ(context.parseError(), HttpParseError::kInvalidFormat);
    }

    {
        HttpContext context;
        CustomHttpParser parser(&context);
        Buffer buf;
        const std::string request =
            "POST / HTTP/1.1\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "0\r\n"
            "\r\n";
        buf.append(request.data(), request.size());

        EXPECT_FALSE(parser.parse(buf));
        EXPECT_EQ(context.parseError(), HttpParseError::kUnsupportedTransferEncoding);
    }
}

/*
测试思路：
1. 模拟 Header 与部分 Body 同批到达，随后用第二次 parse 补齐 Body。
2. 当前 Buffer 耗尽时 parser 必须返回等待状态，不能在 kExpectBody 分支死循环。
3. Body 补齐后才进入 kGotAll，且 Body 字节不能重复或丢失。

示例：
  "hello" = "he" + "llo"
        |
        v
  first parse: gotAll=false, second parse: body="hello"
*/
TEST(TestHttpParserLimits, custom_parser_handles_fragmented_body_without_looping)
{
    HttpParseLimits limits;
    limits.max_body_bytes = 5;

    HttpContext context;
    CustomHttpParser parser(&context, limits);
    Buffer buf;
    const std::string first_part =
        "POST /partial HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "he";
    buf.append(first_part.data(), first_part.size());

    EXPECT_TRUE(parser.parse(buf));
    EXPECT_FALSE(context.gotAll());
    EXPECT_EQ(buf.readableBytes(), 0U);
    EXPECT_EQ(context.request()->bodyString(), "he");

    const std::string second_part = "llo";
    buf.append(second_part.data(), second_part.size());
    EXPECT_TRUE(parser.parse(buf));
    EXPECT_TRUE(context.gotAll());
    EXPECT_EQ(buf.readableBytes(), 0U);
    EXPECT_EQ(context.request()->bodyString(), "hello");
}

/*
测试思路：
1. 普通 Header 之前存在多个 Header 时，后续字段仍应保持独立，不能发生字符串拼接。
2. Content-Length 请求要真正进入 Body 回调并完成解析，覆盖 llhttp 的 Header value complete 与 message complete 路径。

示例：
  Host + Connection + Content-Length + Content-Type
        |
        v
  headers 保持四个独立键，body == "hello"
*/
TEST(TestHttpParserLimits, llhttp_parser_keeps_multiple_headers_independent)
{
    HttpContext context;
    Buffer buf;
    const std::string request =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "hello";
    buf.append(request.data(), request.size());

    EXPECT_TRUE(context.parseRequest(buf, TimeStamp::Now()));
    EXPECT_TRUE(context.gotAll());
    EXPECT_EQ(buf.readableBytes(), 0U);

    const auto request_model = context.request();
    EXPECT_EQ(request_model->getHeader("Host"), "localhost");
    EXPECT_EQ(request_model->getHeader("Connection"), "close");
    EXPECT_EQ(request_model->getHeader("Content-Length"), "5");
    EXPECT_EQ(request_model->getHeader("Content-Type"), "text/plain");
    EXPECT_EQ(request_model->bodyString(), "hello");
}

/*
测试思路：
1. 喂入 llhttp 无法接受的 Header 格式。
2. llhttp 自身错误与主动限制错误都必须在 HttpContext 中留下非 kNone 的错误类型，便于 HTTP Server 映射状态码。
3. 原始报文捕获必须有上限。

示例：
  "Broken-Header" (缺少冒号) -> kInvalidFormat, raw_capture <= 1024
*/
TEST(TestHttpParserLimits, llhttp_parser_records_builtin_format_errors)
{
    HttpContext context;
    Buffer buf;
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Broken-Header\r\n"
        "\r\n";
    buf.append(request.data(), request.size());

    EXPECT_FALSE(context.parseRequest(buf, TimeStamp::Now()));
    EXPECT_EQ(context.parseError(), HttpParseError::kInvalidFormat);
    EXPECT_LE(context.rawCapture().size(), 1024U);
}

/*
测试思路：
1. 通过 Custom parser 验证 Header 名称和值的首尾空白会被规范化。
2. Request 和 Response 两条解析路径都要覆盖，避免只修复其中一侧。

示例：
  " Host : localhost " -> request.getHeader("Host") == "localhost"
  " Content-Type : text/plain " -> response.getHeader("Content-Type") == "text/plain"
*/
TEST(TestHttpMessage, custom_parser_normalizes_request_and_response_headers)
{
    {
        HttpContext context;
        CustomHttpParser parser(&context);
        Buffer buf;
        const std::string request =
            "GET / HTTP/1.1\r\n"
            " Host : localhost \r\n"
            "\r\n";
        buf.append(request.data(), request.size());

        EXPECT_TRUE(parser.parse(buf));
        EXPECT_TRUE(context.gotAll());
        EXPECT_EQ(context.request()->getHeader("Host"), "localhost");
    }

    {
        HttpContext context;
        CustomHttpParser parser(&context);
        parser.setType(HttpParser::RespType);
        Buffer buf;
        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            " Content-Type : text/plain \r\n"
            "\r\n";
        buf.append(response.data(), response.size());

        EXPECT_TRUE(parser.parse(buf));
        EXPECT_TRUE(context.gotAll());
        EXPECT_EQ(context.response()->getHeader("Content-Type"), "text/plain");
    }
}
