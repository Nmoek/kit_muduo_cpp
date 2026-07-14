/**
 * @file protocol_interaction_observe.h
 * @brief 协议项交互观察原始数据adapter层
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-07 19:32:01
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __PROTOCOL_INTERACTION_OBSERVE_H__
#define __PROTOCOL_INTERACTION_OBSERVE_H__

#include "domain/protocol_interaction.h"
#include "domain/type.h"
#include "net/http/http_servlet.h"

namespace kit_domain {


struct InteractionSideCapture
{
    nlohmann::json meta = nlohmann::json::object();

    std::string head_text;

    std::vector<uint8_t> body_bytes;
    std::vector<uint8_t> raw_bytes;

    /// @brief TODO 注意这个字段的实际作用：告诉前段当前收到的请求Body应该怎么解析的问题 展示的
    ProtocolBodyType expect_body_type{ProtocolBodyType::kNone};
    std::string media_type;
    bool prefer_hex_text_for_binary{false};

    bool hasBody() const { return !body_bytes.empty(); }
    bool hasRaw() const { return !raw_bytes.empty(); }
};

struct ProtocolInteractionObservation
{
    InteractionScope scope{InteractionScope::kUnknown};
    int64_t project_id{0};
    int64_t protocol_id{0};
    ProtocolType protocol_type{ProtocolType::kUnknown};

    int64_t time_ms{0};
    std::string peer_addr;
    InteractionResult result{InteractionResult::kRouteNotFound};
    std::string error_message;

    InteractionSideCapture request;
    InteractionSideCapture response;

    static InteractionResult ToInterResult(const kit_muduo::http::MatchStatus &match_status)
    {
        if(kit_muduo::http::MatchStatus::kFound == match_status)
        {
            return InteractionResult::kMatched;
        }
        else if(kit_muduo::http::MatchStatus::kPathFoundMethodNotAllowed == match_status)
        {
            return InteractionResult::kMethodNotAllowed;
        }
        else if(kit_muduo::http::MatchStatus::kNotFound == match_status)
        {
            return InteractionResult::kRouteNotFound;
        }

        return InteractionResult::kInternalError;
    }
};


}
#endif // __PROTOCOL_INTERACTION_OBSERVE_H__