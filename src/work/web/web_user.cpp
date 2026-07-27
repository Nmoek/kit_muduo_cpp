#include "web/web_user.h"

#include "base/time_stamp.h"
#include "domain/user.h"
#include "net/http/http_context.h"
#include "net/http/http_response.h"
#include "net/http/http_server.h"
#include "service/svc_user.h"
#include "web/web_common.h"

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;

namespace kit_domain {

namespace {
struct UserListReq {
    int32_t offset{0};
    int32_t limit{20};
    std::string status{"all"};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(UserListReq, offset, limit, status)
};

struct UserEditReq {
    std::string note_name;
    std::string role;
    std::string status;
    std::string password;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(UserEditReq, note_name, role, status, password)
};

UserStatus ListStatusFromString(const std::string &status)
{
    if(status == "all") return UserStatus::kUnknown;
    return UserStatusFromString(status);
}

nljson UserJson(const User &user)
{
    return nljson{
        {"user_id", user.id},
        {"note_name", user.note_name},
        {"role", UserRoleToString(user.role)},
        {"status", UserStatusToString(user.status)},
        {"ctime", TimeStamp(user.ctime).toUtcRfc3339()},
        {"utime", TimeStamp(user.utime).toUtcRfc3339()},
    };
}

}

UserHandler::UserHandler(std::shared_ptr<UserService> user_svc)
    :user_svc_(std::move(user_svc))
{
}

void UserHandler::RegisterRoutes(std::shared_ptr<HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&UserHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)
    
    server->Post("/users/list", XX(List));
    server->Post("/users/add", XX(Add));
    server->Get("/users/:user_id", XX(Get));
    server->Post("/users/:user_id", XX(Update));
    server->Delete("/users/:user_id", XX(Disable));
    server->Post("/users/:user_id/restore", XX(Restore));
#undef XX
}

void UserHandler::List(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    UserListReq request;
    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        WriteOkJsonResponse(ctx, {{"code", -200}, {"message", "body parse error"}, {"data", nljson::object()}});
        return;
    }

    const auto users = user_svc_->List(ctx, UserListFilter{ListStatusFromString(request.status), request.offset, request.limit});
    nljson data = nljson::array();
    for(const auto &user : users)
    {
        data.push_back(UserJson(user));
    }
    WriteOkJsonResponse(ctx, {{"code", 0}, {"message", "success"}, {"data", data}});
}

void UserHandler::Add(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    UserEditReq request;
    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        WriteOkJsonResponse(ctx, {{"code", -200}, {"message", "body parse error"}, {"data", nljson::object()}});
        return;
    }

    const int64_t user_id = user_svc_->AddUser(ctx, UserCreateParam{request.note_name, UserRoleFromString(request.role), request.password});
    if(user_id <= 0)
    {
        WriteOkJsonResponse(ctx, {{"code", -300}, {"message", "service failed"}, {"data", nljson::object()}});
        return;
    }
    WriteOkJsonResponse(ctx, {{"code", 0}, {"message", "success"}, {"data", {{"user_id", user_id}}}});
}

void UserHandler::Get(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    int64_t user_id = 0;
    if(!ParseRouteArithmetic(ctx, "user_id", user_id))
    {
        WriteOkJsonResponse(ctx, {{"code", -200}, {"message", "query param transform fail"}, {"data", nljson::object()}});
        return;
    }
    const User user = user_svc_->GetById(ctx, user_id);
    if(user.id <= 0)
    {
        WriteOkJsonResponse(ctx, {{"code", -300}, {"message", "service failed"}, {"data", nljson::object()}});
        return;
    }
    WriteOkJsonResponse(ctx, {{"code", 0}, {"message", "success"}, {"data", UserJson(user)}});
}

void UserHandler::Update(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    int64_t user_id = 0;
    UserEditReq request;
    auto bind_result = ctx->bindJson(request);
    if(!ParseRouteArithmetic(ctx, "user_id", user_id) || !bind_result.ok)
    {
        WriteOkJsonResponse(ctx, {{"code", -200}, {"message", "request parse error"}, {"data", nljson::object()}});
        return;
    }
    bool ok = user_svc_->UpdateUser(ctx, user_id, UserUpdateParam{
        request.note_name,
        UserRoleFromString(request.role),
        UserStatusFromString(request.status),
        request.password,
    });
    WriteOkJsonResponse(ctx, {{"code", ok ? 0 : -300}, {"message", ok ? "success" : "service failed"}, {"data", nljson::object()}});
}

void UserHandler::Disable(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    int64_t user_id = 0;
    if(!ParseRouteArithmetic(ctx, "user_id", user_id))
    {
        WriteOkJsonResponse(ctx, {{"code", -200}, {"message", "query param transform fail"}, {"data", nljson::object()}});
        return;
    }
    bool ok = user_svc_->DisableUser(ctx, user_id);
    WriteOkJsonResponse(ctx, {{"code", ok ? 0 : -300}, {"message", ok ? "success" : "service failed"}, {"data", nljson::object()}});
}

void UserHandler::Restore(TcpConnectionPtr conn, HttpContextPtr ctx) noexcept
{
    int64_t user_id = 0;
    if(!ParseRouteArithmetic(ctx, "user_id", user_id))
    {
        WriteOkJsonResponse(ctx, {{"code", -200}, {"message", "query param transform fail"}, {"data", nljson::object()}});
        return;
    }
    bool ok = user_svc_->RestoreUser(ctx, user_id);
    WriteOkJsonResponse(ctx, {{"code", ok ? 0 : -300}, {"message", ok ? "success" : "service failed"}, {"data", nljson::object()}});
}

} // namespace kit_domain
