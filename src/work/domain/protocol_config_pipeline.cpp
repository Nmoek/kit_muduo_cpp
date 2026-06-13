/**
 * @file protocol_cfg_pipeline.cpp
 * @brief  协议项配置合法性校验与构建器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-10 18:53:42
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "domain/custom_tcp_protocol_item.h"
#include "domain/domain_log.h"
#include "domain/http_protocol_item.h"
#include "domain/protocol_runtime_key.h"
#include "domain/type.h"
#include "domain/protocol_config_pipeline.h"

namespace kit_domain {

ProtocolConfigCheckResult ProtocolConfigPipeline::checkConfig(const ProtocolSideConfigSpec &spec)
{
    const auto policy = getSidePolicy(spec.type, spec.side);
    if(nullptr == policy)
    {
        return ProtocolConfigCheckResult::Failed("unsupported protocol side config policy");
    }

    auto result = policy->validateAndParse(spec);
    if(!result.ok || !result.parsed)
    {
        return ProtocolConfigCheckResult::Failed(result.message);
    }

    return ProtocolConfigCheckResult::Success(result.parsed->runtime_key);
}

ProtocolItemBuildResult ProtocolConfigPipeline::buildItem(const ProtocolItemBuildSpec &spec)
{
    const auto build_policy = getBuildPolicy(spec.full_config.type);
    if(!build_policy)
    {
        return ProtocolItemBuildResult::Failed("unsupported protocol item build policy");
    }

    const auto req_policy = getSidePolicy(spec.full_config.type, ProtocolSide::kRequest);
    const auto resp_policy = getSidePolicy(spec.full_config.type, ProtocolSide::kResponse);
    if(!req_policy || !resp_policy)
    {
        return ProtocolItemBuildResult::Failed("unsupported protocol side config policy");
    }

    auto req_result = req_policy->validateAndParse(spec.full_config.toSideSpec(ProtocolSide::kRequest, spec.custom_tcp_pattern.get()));
    if(!req_result.ok || !req_result.parsed || !req_result.parsed->runtime_key.has_value())
    {
        return ProtocolItemBuildResult::Failed(req_result.message);
    }

    auto resp_result = resp_policy->validateAndParse(spec.full_config.toSideSpec(ProtocolSide::kResponse, spec.custom_tcp_pattern.get()));
    if(!resp_result.ok || !resp_result.parsed)
    {
        return ProtocolItemBuildResult::Failed(resp_result.message);
    }

    return build_policy->build(spec, *req_result.parsed, *resp_result.parsed);
}

const ProtocolSideConfigPolicy* ProtocolConfigPipeline::getSidePolicy(ProtocolType type, ProtocolSide side)
{
    if(ProtocolType::kHttp == type
        || ProtocolType::kHttps == type)
    {
        if(ProtocolSide::kRequest == side)
        {
            static HttpReqPolicy p;
            return &p;
        }
        else if(ProtocolSide::kResponse == side)
        {
            static HttpRespPolicy p;
            return &p;
        }
    }
    else if(ProtocolType::kCustomTcp == type)
    {
        if(ProtocolSide::kRequest == side)
        {
            static CustomTcpReqPolicy p;
            return &p;
        }
        else if(ProtocolSide::kResponse == side)
        {
            static CustomTcpRespPolicy p;
            return &p;
        }
    }

    return nullptr;
}

const ProtocolItemBuildPolicy* ProtocolConfigPipeline::getBuildPolicy(ProtocolType type)
{
    if(ProtocolType::kHttp == type
        || ProtocolType::kHttps == type)
    {
        static HttpBuildPolicy p;
        return &p;
    }
    else if(ProtocolType::kCustomTcp == type)
    {
        static CustomTcpBuildPolicy p;
        return &p;
    }

    return nullptr;
}

SideConfigParseResult ProtocolSideConfigPolicy::validateAndParse(const ProtocolSideConfigSpec &spec) const
{
    if(!spec.cfg.is_object())
    {
        return SideConfigParseResult::Failed("cfg not object");
    }

    const auto allowed_fields = allowFields();
    for(auto &item : spec.cfg.items())
    {
        if(allowed_fields.find(item.key()) == allowed_fields.end())
        {
            return SideConfigParseResult::Failed("unknown cfg field: " + item.key());
        }
    }

    return parseFields(spec);
}


SideConfigParseResult HttpReqPolicy::parseFields(const ProtocolSideConfigSpec &spec) const
{
    auto parsed = std::make_unique<ParsedHttpReqConfig>();

    if(!parsed->cfg.fromJson(spec.cfg))
    {
        return SideConfigParseResult::Failed("http req cfg invalid");
    }

    auto runtime_key = GenerateProtocolRuntimeKey(spec.type, spec.cfg);
    if(!runtime_key.has_value())
    {
        return SideConfigParseResult::Failed("http generate runtime_key error");
    }
    parsed->runtime_key = runtime_key.value();
    return SideConfigParseResult::Success(std::move(parsed));
}

SideConfigParseResult HttpRespPolicy::parseFields(const ProtocolSideConfigSpec &spec) const 
{
    auto parsed = std::make_unique<ParsedHttpRespConfig>();

    if(!parsed->cfg.fromJson(spec.cfg))
    {
        return SideConfigParseResult::Failed("http resp cfg invalid");
    }

    return SideConfigParseResult::Success(std::move(parsed));
}

SideConfigParseResult CustomTcpReqPolicy::parseFields(const ProtocolSideConfigSpec &spec) const
{
    if(!spec.custom_tcp_pattern_spec)
    {
        return SideConfigParseResult::Failed("custom tcp pattern info is null");
    }

    auto parsed = std::make_unique<ParsedCustomTcpConfg>();

    if(!parsed->cfg.fromJson(spec.cfg, *spec.custom_tcp_pattern_spec))
    {
        return SideConfigParseResult::Failed("custom tcp req cfg invalid");
    }

    auto runtime_key = GenerateProtocolRuntimeKey(spec.type, spec.cfg);
    if(!runtime_key.has_value())
    {
        return SideConfigParseResult::Failed("tcp generate runtime_key error");
    }
    parsed->runtime_key = runtime_key.value();
    return SideConfigParseResult::Success(std::move(parsed));
}

SideConfigParseResult CustomTcpRespPolicy::parseFields(const ProtocolSideConfigSpec &spec) const
{
    if(!spec.custom_tcp_pattern_spec)
    {
        return SideConfigParseResult::Failed("custom tcp pattern info is null");
    }

    auto parsed = std::make_unique<ParsedCustomTcpConfg>();

    if(!parsed->cfg.fromJson(spec.cfg, *spec.custom_tcp_pattern_spec))
    {
        return SideConfigParseResult::Failed("custom tcp req cfg invalid");
    }

    return SideConfigParseResult::Success(std::move(parsed));
}

ProtocolItemBuildResult HttpBuildPolicy::build(const ProtocolItemBuildSpec &spec,
    const ParsedSideConfig& req,
    const ParsedSideConfig& resp) const
{
    const auto& http_req = dynamic_cast<const ParsedHttpReqConfig&>(req);
    const auto& http_resp = dynamic_cast<const ParsedHttpRespConfig&>(resp);

    auto http_item = std::make_shared<HttpProtocolItem>();
    if(!http_item)
    {
        return ProtocolItemBuildResult::Failed("http protocol item create error");
    }
    http_item->init(spec.ori_protocol, http_req.cfg, http_resp.cfg);

    return ProtocolItemBuildResult::Success(http_req.runtime_key, std::move(http_item));
}

ProtocolItemBuildResult CustomTcpBuildPolicy::build(const ProtocolItemBuildSpec &spec,
    const ParsedSideConfig& req,
    const ParsedSideConfig& resp) const
{

    if(!spec.custom_tcp_pattern)
    {
        return ProtocolItemBuildResult::Failed("custom tcp pattern is null");
    }

    const auto& tcp_req = dynamic_cast<const ParsedCustomTcpConfg&>(req);
    const auto& tcp_resp = dynamic_cast<const ParsedCustomTcpConfg&>(resp);

    auto tcp_item = std::make_shared<CustomTcpProtocolItem>(spec.custom_tcp_pattern);
    if(!tcp_item)
    {
        return ProtocolItemBuildResult::Failed("tcp protocol item create error");
    }
    tcp_item->init(spec.ori_protocol, tcp_req.cfg, tcp_resp.cfg);

    return ProtocolItemBuildResult::Success(tcp_req.runtime_key, std::move(tcp_item));
}

}