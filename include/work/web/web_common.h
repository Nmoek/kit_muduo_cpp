/**
 * @file web_common.h
 * @brief web层公共辅助接口
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-23 15:28:43
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WEB_COMMON_H__
#define __KIT_WEB_COMMON_H__

#include "domain/type.h"
#include "net/call_backs.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <string>

namespace kit_domain {

class ProjectSvcInterface;
class ProtocolSvcInterface;
struct ProtocolAccessInfo;

void WriteJsonResponse(kit_muduo::HttpContextPtr ctx, const nlohmann::json& root);
void WriteOkJsonResponse(kit_muduo::HttpContextPtr ctx, const nlohmann::json& root);
void WriteJsonError(kit_muduo::HttpContextPtr ctx,
                    int32_t code,
                    const std::string& message,
                    bool include_empty_data = false);
void WriteForbidden(kit_muduo::HttpContextPtr ctx);

bool ParsePositiveInt64(const std::string& value, int64_t& out);
bool ParseRouteInt64(kit_muduo::HttpContextPtr ctx, const std::string& name, int64_t& out);
bool ParseProtocolSide(const std::string& value, ProtocolSide& side);
bool ParseProtocolSideFromQuery(kit_muduo::HttpContextPtr ctx, ProtocolSide& side);

bool CheckProjectAccess(kit_muduo::HttpContextPtr ctx,
                        ProjectSvcInterface* project_svc,
                        int64_t project_id,
                        bool require_active,
                        bool admin_only);

bool CheckProtocolAccess(kit_muduo::HttpContextPtr ctx,
                         ProtocolSvcInterface* protocol_svc,
                         int64_t protocol_id,
                         bool require_active,
                         bool admin_only,
                         ProtocolAccessInfo& access_info);

} // namespace kit_domain

#endif // __KIT_WEB_COMMON_H__
