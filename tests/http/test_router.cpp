/**
 * @file test_router.cpp
 * @brief 路由匹配处理
 * @author ljk5
 * @version 1.0
 * @date 2025-08-14 17:14:32
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "gtest/gtest.h"
#include "../test_log.h"
#include "net/event_loop.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_servlet.h"
#include "net/http/http_util.h"
#include "net/inet_address.h"
#include "net/tcp_connection.h"

#include <memory>
#include <regex>
#include <string>

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace {

class TextServlet : public HttpServlet
{
public:
    explicit TextServlet(std::string body)
        :HttpServlet("TextServlet")
        ,body_(std::move(body))
    {}

    void handle(TcpConnectionPtr conn, HttpContextPtr ctx) override
    {
        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);
        resp->setStateCode(StateCode::k200Ok);
        resp->body().appendData(body_);
    }

private:
    std::string body_;
};

struct DispatchFixture
{
    EventLoop loop;
    HttpServletDispatch dispatch;
    TcpConnectionPtr conn{std::make_shared<TcpConnection>(&loop, "test", 0, InetAddress(), InetAddress())};

    HttpResponsePtr Request(const std::string &path, int32_t method)
    {
        auto ctx = std::make_shared<HttpContext>();
        ctx->request()->setPath(path);
        ctx->request()->setMethod(method);
        dispatch.handle(conn, ctx);
        return ctx->response();
    }
};

HttpServlet::Ptr Servlet(const std::string &body)
{
    return std::make_shared<TextServlet>(body);
}

} // namespace

TEST(TestRouter, regex)
{
    std::string ori_pattern = "/projects/query/:id/:other";
    auto regex_pattern = std::regex_replace(ori_pattern, std::regex(":[^/]+"), "([^/]+)");
    // auto regex_pattern = std::regex_replace(ori_pattern, std::regex(":[^/]+"), R"((\d+))");

    regex_pattern = "^" + regex_pattern + "$";

    TEST_INFO() << ori_pattern << " --> " << regex_pattern << std::endl;
    std::regex _regex(regex_pattern);

    std::smatch matches;
    std::string url = "/projects/query/1/6";
    std::regex param_rex(":([^/]+)");
    // std::regex param_rex(":([^/(0-9)]+)");

    if (std::regex_match(url, matches, _regex))
    {
        // 提取动态参数
        auto param_it = std::sregex_iterator(ori_pattern.begin(), ori_pattern.end(), param_rex);
        auto match_it = ++matches.begin();

        TEST_INFO() << ori_pattern << " --> " << regex_pattern << std::endl;

        while(param_it != std::sregex_iterator() && match_it != matches.end())
        {
            std::cout << (*param_it)[1].str() << " : " << match_it->str() << std::endl;
            ++param_it;
            ++match_it;
        }
    }
    else
    {
        TEST_INFO() << "no match" << std::endl;
    }

}

TEST(TestRouter, MethodMaskHelpers)
{
    ASSERT_EQ(ToMethodMask(HttpRequest::Method(HttpRequest::Method::kGet)), ExpectHttpMethods::Get);
    ASSERT_EQ(ToMethodMask(HttpRequest::Method(HttpRequest::Method::kPost)), ExpectHttpMethods::Post);
    ASSERT_TRUE(MethodAllowed(ExpectHttpMethods::Get | ExpectHttpMethods::Post, HttpRequest::Method(HttpRequest::Method::kGet)));
    ASSERT_FALSE(MethodAllowed(ExpectHttpMethods::Get, HttpRequest::Method(HttpRequest::Method::kDelete)));
    ASSERT_EQ(BuildAllowHeader(ExpectHttpMethods::Get | ExpectHttpMethods::Post | ExpectHttpMethods::Delete), "GET, POST, DELETE");
}

TEST(TestRouter, ExactRoutesCanBindDifferentMethods)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/items", Servlet("get")).ok());
    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Post, "/items", Servlet("post")).ok());

    auto get_resp = f.Request("/items", HttpRequest::Method::kGet);
    ASSERT_EQ(get_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(get_resp->body().toString(), "get");

    auto post_resp = f.Request("/items", HttpRequest::Method::kPost);
    ASSERT_EQ(post_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(post_resp->body().toString(), "post");
}

TEST(TestRouter, ExactRouteCanBindMethodMask)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, "/items", Servlet("both")).ok());

    auto get_resp = f.Request("/items", HttpRequest::Method::kGet);
    ASSERT_EQ(get_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(get_resp->body().toString(), "both");

    auto post_resp = f.Request("/items", HttpRequest::Method::kPost);
    ASSERT_EQ(post_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(post_resp->body().toString(), "both");
}

TEST(TestRouter, ExactRouteRejectsOverlappingMethodMasks)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, "/items", Servlet("both")).ok());
    auto conflict = f.dispatch.addRoute(ExpectHttpMethods::Get, "/items", Servlet("get"));

    ASSERT_FALSE(conflict.ok());
    ASSERT_EQ(conflict.status, RouteStatus::Conflict);
}

TEST(TestRouter, NotFoundAndMethodNotAllowedAreDistinct)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, "/items", Servlet("items")).ok());

    auto not_found = f.Request("/missing", HttpRequest::Method::kGet);
    ASSERT_EQ(not_found->stateCode().toInt(), StateCode::k404NotFound);

    auto method_not_allowed = f.Request("/items", HttpRequest::Method::kDelete);
    ASSERT_EQ(method_not_allowed->stateCode().toInt(), StateCode::k405MethodNotAllowed);
    ASSERT_EQ(method_not_allowed->getHeader("Allow"), "GET, POST");
    ASSERT_EQ(method_not_allowed->getHeader("Content-Length"), "0");
    ASSERT_NE(method_not_allowed->toString().find("Content-Length: 0\r\n"), std::string::npos);
}

TEST(TestRouter, DynamicRouteExtractsParams)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/test/:id", [](TcpConnectionPtr conn, HttpContextPtr ctx) {
        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);
        resp->setStateCode(StateCode::k200Ok);
        resp->body().appendData(ctx->routeParam("id"));
    }).ok());

    auto resp = f.Request("/test/123", HttpRequest::Method::kGet);
    ASSERT_EQ(resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(resp->body().toString(), "123");
}

TEST(TestRouter, DynamicRouteParamsDoNotOverwriteQueryParams)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/users/:id", [](TcpConnectionPtr conn, HttpContextPtr ctx) {
        auto resp = ctx->response();
        resp->setVersion(Version::kHttp11);
        resp->setStateCode(StateCode::k200Ok);
        resp->body().appendData(ctx->routeParam("id"));
        resp->body().appendData("|");
        resp->body().appendData(ctx->request()->getQureyParam("id"));
    }).ok());

    auto ctx = std::make_shared<HttpContext>();
    ctx->request()->setPath("/users/123");
    ctx->request()->setMethod(HttpRequest::Method::kGet);
    ctx->request()->addQureyParam("id", "456");
    f.dispatch.handle(f.conn, ctx);

    auto resp = ctx->response();
    ASSERT_EQ(resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(resp->body().toString(), "123|456");
}

TEST(TestRouter, DynamicRouteEscapesStaticRegexChars)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/api/v1.0/:id", Servlet("match")).ok());

    auto miss_resp = f.Request("/api/v1x0/123", HttpRequest::Method::kGet);
    ASSERT_EQ(miss_resp->stateCode().toInt(), StateCode::k404NotFound);

    auto hit_resp = f.Request("/api/v1.0/123", HttpRequest::Method::kGet);
    ASSERT_EQ(hit_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(hit_resp->body().toString(), "match");
}

TEST(TestRouter, ColonInsideStaticSegmentIsExactRoute)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/v1:batchGet", Servlet("exact")).ok());

    auto exact_resp = f.Request("/v1:batchGet", HttpRequest::Method::kGet);
    ASSERT_EQ(exact_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(exact_resp->body().toString(), "exact");

    auto miss_resp = f.Request("/v1anything", HttpRequest::Method::kGet);
    ASSERT_EQ(miss_resp->stateCode().toInt(), StateCode::k404NotFound);
}

TEST(TestRouter, GlobWildcardInsideStaticSegmentIsExactRoute)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/files/v1*beta", Servlet("exact")).ok());

    auto exact_resp = f.Request("/files/v1*beta", HttpRequest::Method::kGet);
    ASSERT_EQ(exact_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(exact_resp->body().toString(), "exact");

    auto miss_resp = f.Request("/files/v1ZZbeta", HttpRequest::Method::kGet);
    ASSERT_EQ(miss_resp->stateCode().toInt(), StateCode::k404NotFound);
}

TEST(TestRouter, DynamicRouteCanBindMethodMask)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, "/test/:id", Servlet("both")).ok());

    auto get_resp = f.Request("/test/1", HttpRequest::Method::kGet);
    ASSERT_EQ(get_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(get_resp->body().toString(), "both");

    auto post_resp = f.Request("/test/2", HttpRequest::Method::kPost);
    ASSERT_EQ(post_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(post_resp->body().toString(), "both");
}

TEST(TestRouter, DynamicRouteRejectsSamePatternMethodOverlap)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/test/:id", Servlet("get")).ok());
    auto conflict = f.dispatch.addRoute(ExpectHttpMethods::Get, "/test/:id", Servlet("get2"));

    ASSERT_FALSE(conflict.ok());
    ASSERT_EQ(conflict.status, RouteStatus::Conflict);
}

TEST(TestRouter, DynamicRouteReturnsMethodNotAllowed)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/test/:id", Servlet("get")).ok());

    auto resp = f.Request("/test/1", HttpRequest::Method::kDelete);
    ASSERT_EQ(resp->stateCode().toInt(), StateCode::k405MethodNotAllowed);
    ASSERT_EQ(resp->getHeader("Allow"), "GET");
}

TEST(TestRouter, ExactRouteHasPriorityOverDynamicRoute)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/test/:id", Servlet("dynamic")).ok());
    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/test/list", Servlet("exact")).ok());

    auto resp = f.Request("/test/list", HttpRequest::Method::kGet);
    ASSERT_EQ(resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(resp->body().toString(), "exact");
}

TEST(TestRouter, GlobRouteCanMatchStaticFiles)
{
    DispatchFixture f;

    ASSERT_TRUE(f.dispatch.addRoute(ExpectHttpMethods::Get, "/html/*.html", Servlet("glob")).ok());

    auto resp = f.Request("/html/index.html", HttpRequest::Method::kGet);
    ASSERT_EQ(resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(resp->body().toString(), "glob");

    auto nested_resp = f.Request("/html/nested/index.html", HttpRequest::Method::kGet);
    ASSERT_EQ(nested_resp->stateCode().toInt(), StateCode::k404NotFound);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
