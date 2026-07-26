/**
 * @file web_common.cpp
 * @brief web层公共辅助接口
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-23 15:28:19
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "web/web_common.h"
#include "base/util.h"
#include "domain/project.h"
#include "domain/protocol.h"
#include "domain/user.h"
#include "net/http/http_context.h"
#include "net/http/http_response.h"
#include "net/http/http_util.h"
#include "service/svc_project.h"
#include "service/svc_protocol.h"

#include <stdexcept>

using namespace kit_muduo;

namespace kit_domain {

void WriteJsonResponse(kit_muduo::HttpContextPtr ctx, const nlohmann::json& root)
{
    ctx->response()->setJson(root);
}

void WriteOkJsonResponse(kit_muduo::HttpContextPtr ctx, const nlohmann::json& root)
{
    auto resp = ctx->response();
    resp->setVersion(kit_muduo::http::Version::kHttp11);
    resp->setStateCode(kit_muduo::http::StateCode::k200Ok);
    resp->setJson(root);
}

void WriteJsonError(kit_muduo::HttpContextPtr ctx,
                    int32_t code,
                    const std::string& message,
                    bool include_empty_data)
{
    nlohmann::json root = {
        {"code", code},
        {"message", message},
    };
    if(include_empty_data)
    {
        root["data"] = nlohmann::json::object();
    }
    WriteJsonResponse(ctx, root);
}

void WriteForbidden(kit_muduo::HttpContextPtr ctx)
{
    auto resp = ctx->response();
    resp->setStateCode(kit_muduo::http::StateCode::k403Forbidden);
    WriteJsonResponse(ctx, {
        {"code", -403},
        {"message", "forbidden"},
        {"data", nlohmann::json::object()},
    });
}



bool ParseProtocolSide(const std::string& value, ProtocolSide& side)
{
    int64_t side_val = 0;
    if(!ParsePositiveArithmetic(value, side_val))
    {
        return false;
    }

    if(static_cast<int64_t>(ProtocolSide::kRequest) == side_val
        || static_cast<int64_t>(ProtocolSide::kResponse) == side_val)
    {
        side = static_cast<ProtocolSide>(side_val);
        return true;
    }
    return false;
}

bool ParseProtocolSideFromQuery(kit_muduo::HttpContextPtr ctx, ProtocolSide& side)
{
    if(ParseProtocolSide(ctx->queryParam("side"), side))
    {
        return true;
    }
    return ParseProtocolSide(ctx->queryParam("req_or_resp"), side);
}

bool CheckProjectAccess(kit_muduo::HttpContextPtr ctx,
                        ProjectSvcInterface* project_svc,
                        int64_t project_id,
                        bool require_active,
                        bool admin_only)
{
    const auto current_user = CurrentUserFromContext(ctx);
    if(admin_only && !current_user.IsAdmin())
    {
        return false;
    }
    if(!project_svc || project_id <= 0)
    {
        return false;
    }

    const auto project = project_svc->GetById(ctx, project_id);
    if(project.m_id <= 0)
    {
        return false;
    }
    if(require_active && project.m_status != ProjectStatus::kValid)
    {
        return false;
    }
    return current_user.IsAdmin() || project.m_userId == current_user.user_id;
}

bool CheckProtocolAccess(kit_muduo::HttpContextPtr ctx,
                         ProtocolSvcInterface* protocol_svc,
                         int64_t protocol_id,
                         bool require_active,
                         bool admin_only,
                         ProtocolAccessInfo& access_info)
{
    if(!protocol_svc || protocol_id <= 0)
    {
        return false;
    }

    if(!protocol_svc->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return false;
    }

    const auto current_user = CurrentUserFromContext(ctx);
    if(admin_only && !current_user.IsAdmin())
    {
        return false;
    }
    if(require_active
        && (ProjectStatus::kValid != access_info.project_status
            || ProtocolStatus::kValid != access_info.protocol_status))
    {
        return false;
    }

    return current_user.IsAdmin() || access_info.project_user_id == current_user.user_id;
}

} // namespace kit_domain
