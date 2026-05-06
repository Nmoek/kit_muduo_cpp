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

// ==================================================================
// 删除/查询接口测试
// ==================================================================

TEST(TestRouter, RemoveRouteById_ExactRoute)
{
    DispatchFixture f;

    auto result = f.dispatch.addRoute(ExpectHttpMethods::Get, "/api/items", Servlet("items"));
    ASSERT_TRUE(result.ok());

    // 删除前可正常访问
    auto before = f.Request("/api/items", HttpRequest::Method::kGet);
    ASSERT_EQ(before->stateCode().toInt(), StateCode::k200Ok);

    // 按 id 删除
    ASSERT_TRUE(f.dispatch.removeRoute(result.route_id));

    // 删除后返回 404
    auto after = f.Request("/api/items", HttpRequest::Method::kGet);
    ASSERT_EQ(after->stateCode().toInt(), StateCode::k404NotFound);
}

TEST(TestRouter, RemoveRouteById_DynamicRoute)
{
    DispatchFixture f;

    auto result = f.dispatch.addRoute(ExpectHttpMethods::Get, "/user/:id", Servlet("user"));
    ASSERT_TRUE(result.ok());

    auto before = f.Request("/user/42", HttpRequest::Method::kGet);
    ASSERT_EQ(before->stateCode().toInt(), StateCode::k200Ok);

    ASSERT_TRUE(f.dispatch.removeRoute(result.route_id));

    auto after = f.Request("/user/42", HttpRequest::Method::kGet);
    ASSERT_EQ(after->stateCode().toInt(), StateCode::k404NotFound);
}

TEST(TestRouter, RemoveRouteById_NonexistentId)
{
    DispatchFixture f;

    // 删除不存在的 id 返回 false
    ASSERT_FALSE(f.dispatch.removeRoute(99999));
}

TEST(TestRouter, RemoveRouteByPatternAndMethod)
{
    DispatchFixture f;

    f.dispatch.addRoute(ExpectHttpMethods::Get, "/api/data", Servlet("get"));
    f.dispatch.addRoute(ExpectHttpMethods::Post, "/api/data", Servlet("post"));

    // 删 GET，POST 仍可用
    size_t removed = f.dispatch.removeRoute("/api/data", ExpectHttpMethods::Get);
    ASSERT_EQ(removed, 1u);

    // GET 已删但 POST 仍占用该 path，应返回 405（非 404）
    auto get_resp = f.Request("/api/data", HttpRequest::Method::kGet);
    ASSERT_EQ(get_resp->stateCode().toInt(), StateCode::k405MethodNotAllowed);

    auto post_resp = f.Request("/api/data", HttpRequest::Method::kPost);
    ASSERT_EQ(post_resp->stateCode().toInt(), StateCode::k200Ok);
    ASSERT_EQ(post_resp->body().toString(), "post");
}

TEST(TestRouter, RemoveRouteByPatternAndMethod_NoMatch)
{
    DispatchFixture f;

    f.dispatch.addRoute(ExpectHttpMethods::Get, "/only", Servlet("get"));

    // method 不匹配，删除 0 条
    size_t removed = f.dispatch.removeRoute("/only", ExpectHttpMethods::Post);
    ASSERT_EQ(removed, 0u);

    // 路由仍在
    auto resp = f.Request("/only", HttpRequest::Method::kGet);
    ASSERT_EQ(resp->stateCode().toInt(), StateCode::k200Ok);
}

TEST(TestRouter, RemoveRouteByPattern_AllMethods)
{
    DispatchFixture f;

    f.dispatch.addRoute(ExpectHttpMethods::Get, "/multi", Servlet("get"));
    f.dispatch.addRoute(ExpectHttpMethods::Post, "/multi", Servlet("post"));
    f.dispatch.addRoute(ExpectHttpMethods::Delete, "/other", Servlet("other"));

    // 按 pattern 删除，/multi 下 2 条全部移除
    size_t removed = f.dispatch.removeRoute("/multi");
    ASSERT_EQ(removed, 2u);

    // /multi 的 GET、POST 都 404
    ASSERT_EQ(f.Request("/multi", HttpRequest::Method::kGet)->stateCode().toInt(), StateCode::k404NotFound);
    ASSERT_EQ(f.Request("/multi", HttpRequest::Method::kPost)->stateCode().toInt(), StateCode::k404NotFound);

    // /other 不受影响
    ASSERT_EQ(f.Request("/other", HttpRequest::Method::kDelete)->stateCode().toInt(), StateCode::k200Ok);
}

TEST(TestRouter, ListRoutes_Empty)
{
    DispatchFixture f;

    auto routes = f.dispatch.listRoutes();
    ASSERT_TRUE(routes.empty());
}

TEST(TestRouter, ListRoutes_WithRegistrations)
{
    DispatchFixture f;

    f.dispatch.addRoute(ExpectHttpMethods::Get, "/a", Servlet("a"));
    f.dispatch.addRoute(ExpectHttpMethods::Post, "/b", Servlet("b"));
    f.dispatch.addRoute(ExpectHttpMethods::Get | ExpectHttpMethods::Post, "/c", Servlet("c"));

    auto routes = f.dispatch.listRoutes();
    ASSERT_EQ(routes.size(), 3u);

    // 验证 methods_str 可读
    for (const auto &r : routes)
    {
        ASSERT_FALSE(r.methods_str.empty());
        ASSERT_NE(r.id, 0u);
    }
}

TEST(TestRouter, ListRoutes_ByPattern)
{
    DispatchFixture f;

    f.dispatch.addRoute(ExpectHttpMethods::Get, "/shared", Servlet("get"));
    f.dispatch.addRoute(ExpectHttpMethods::Post, "/shared", Servlet("post"));
    f.dispatch.addRoute(ExpectHttpMethods::Get, "/unrelated", Servlet("other"));

    auto shared = f.dispatch.listRoutes("/shared");
    ASSERT_EQ(shared.size(), 2u);

    auto none = f.dispatch.listRoutes("/notexist");
    ASSERT_TRUE(none.empty());
}

TEST(TestRouter, GetRoute_ById)
{
    DispatchFixture f;

    auto result = f.dispatch.addRoute(ExpectHttpMethods::Get, "/target", Servlet("hit"));
    ASSERT_TRUE(result.ok());

    auto info = f.dispatch.getRoute(result.route_id);
    ASSERT_NE(info.id, 0u);
    ASSERT_EQ(info.pattern, "/target");
    ASSERT_EQ(info.methods, ExpectHttpMethods::Get);
    ASSERT_EQ(info.methods_str, "GET");
}

TEST(TestRouter, GetRoute_ByIdNotFound)
{
    DispatchFixture f;

    auto info = f.dispatch.getRoute(99999);
    ASSERT_EQ(info.id, 0u); // id==0 表示未找到
}

TEST(TestRouter, ListRoutes_AfterRemoveByIdDoesNotAppear)
{
    DispatchFixture f;

    auto r1 = f.dispatch.addRoute(ExpectHttpMethods::Get, "/keep", Servlet("keep"));
    auto r2 = f.dispatch.addRoute(ExpectHttpMethods::Post, "/remove", Servlet("remove"));

    ASSERT_TRUE(f.dispatch.removeRoute(r2.route_id));

    auto routes = f.dispatch.listRoutes();
    ASSERT_EQ(routes.size(), 1u);
    ASSERT_EQ(routes[0].id, r1.route_id);
    ASSERT_EQ(routes[0].pattern, "/keep");
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
