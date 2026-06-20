#include "web/web_auth.h"

#include "domain/user.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_server.h"
#include "service/svc_auth.h"

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;

namespace kit_domain {

namespace {
constexpr const char *kSessionCookieName = "kit_session";
constexpr const char *kSessionCookieOptions = "; HttpOnly; SameSite=Strict; Path=/; Max-Age=43200";
constexpr const char *kExpiredSessionCookie = "kit_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";

struct LoginReq {
    std::string note;
    std::string login_type;
    std::string password;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginReq, note, login_type, password)
};

nljson CurrentUserJson(const CurrentUser &user)
{
    return nljson{
        {"user_id", user.user_id},
        {"note_name", user.note_name},
        {"role", UserRoleToString(user.role)},
        {"status", UserStatusToString(user.status)},
    };
}

void WriteJson(HttpContextPtr ctx, const nljson &root)
{
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);
    resp->body().appendData(root.dump());
}
}

AuthHandler::AuthHandler(std::shared_ptr<AuthService> auth_svc)
    :auth_svc_(std::move(auth_svc))
{
}

void AuthHandler::RegisterRoutes(std::shared_ptr<HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&AuthHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)
    server->Post("/auth/login", XX(Login));
    server->Post("/auth/logout", XX(Logout));
    server->Get("/auth/me", XX(Me));
#undef XX
}

void AuthHandler::Login(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    LoginReq request;
    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        WriteJson(ctx, {{"code", -200}, {"message", "body parse error"}, {"data", nljson::object()}});
        return;
    }

    LoginResult result = auth_svc_->Login(ctx, LoginRequest{request.note, request.login_type, request.password});
    if(!result.ok)
    {
        WriteJson(ctx, {{"code", -300}, {"message", result.message}, {"data", nljson::object()}});
        return;
    }

    auto resp = ctx->response();
    resp->addHeader("Set-Cookie", std::string(kSessionCookieName) + "=" + result.cookie_value + kSessionCookieOptions);
    WriteJson(ctx, {{"code", 0}, {"message", "success"}, {"data", CurrentUserJson(result.user)}});
}

void AuthHandler::Logout(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    const std::string cookie = ExtractCookieValue(ctx->request()->getHeader("Cookie"), kSessionCookieName);
    auth_svc_->Logout(ctx, cookie);
    ctx->response()->addHeader("Set-Cookie", kExpiredSessionCookie);
    WriteJson(ctx, {{"code", 0}, {"message", "success"}, {"data", nljson::object()}});
}

void AuthHandler::Me(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    WriteJson(ctx, {{"code", 0}, {"message", "success"}, {"data", CurrentUserJson(CurrentUserFromContext(ctx))}});
}

} // namespace kit_domain
