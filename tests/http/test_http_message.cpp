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
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
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
