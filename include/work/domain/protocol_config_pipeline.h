/**
 * @file protocol_config_pipeline.h
 * @brief 协议项配置合法性校验与构建器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-10 19:11:03
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_PROTOCOL_CFG_PIPELINE_H__ 
#define __KIT_PROTOCOL_CFG_PIPELINE_H__

#include "domain/custom_tcp_protocol_item.h"
#include "domain/http_protocol_item.h"
#include "domain/type.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_pattern_spec.h"

#include <optional>
#include <unordered_set>
#include <string>

namespace kit_domain {

class ProtocolItem;

struct ProtocolSideConfigSpec
{
    ProtocolType type{ProtocolType::kUnknown};
    ProtocolSide side{ProtocolSide::kRequest};
    const nlohmann::json &cfg;

    std::optional<CustomTcpPatternSpec> custom_tcp_pattern_spec;
};

struct ProtocolFullConfigSpec
{
    ProtocolType type{ProtocolType::kUnknown};
    const nlohmann::json &req_cfg;
    const nlohmann::json &resp_cfg;

    ProtocolSideConfigSpec toSideSpec(ProtocolSide side, const CustomTcpPattern* pattern) const
    {
        if(ProtocolSide::kRequest == side)
        {
            return ProtocolSideConfigSpec{
                .type = this->type,
                .side = ProtocolSide::kRequest,
                .cfg = this->req_cfg,
                .custom_tcp_pattern_spec = (pattern ? std::make_optional(pattern->spec()) : std::nullopt)
            };
        }

        return ProtocolSideConfigSpec{
            .type = this->type,
            .side = ProtocolSide::kResponse,
            .cfg = this->resp_cfg,
            .custom_tcp_pattern_spec = (pattern ? std::make_optional(pattern->spec()) : std::nullopt)
        };
    } 
};

struct ProtocolItemBuildSpec
{
    ProtocolFullConfigSpec full_config;
    const Protocol& ori_protocol;

    // 第一阶段 CustomTcpProtocolItem 构造仍需要 CustomTcpPattern。
    // 这里暂不展开更多上下文对象，只保留当前构建必需依赖。
    std::shared_ptr<CustomTcpPattern> custom_tcp_pattern{nullptr};
};

struct ProtocolConfigCheckResult
{
    bool ok{false};
    std::string message;
    std::optional<std::string> runtime_key;

    static ProtocolConfigCheckResult Failed(const std::string &msg = "check invalid")
    {
        return {false, msg, std::nullopt};
    }

    static ProtocolConfigCheckResult Success(std::optional<std::string> runtime_key = std::nullopt, const std::string &msg = "check ok")
    {
        return {true, msg, runtime_key};
    }

};


struct ProtocolItemBuildResult
{
    bool ok{false};
    std::string message;
    std::optional<std::string> runtime_key;
    std::shared_ptr<ProtocolItem> item;

    static ProtocolItemBuildResult Failed(const std::string &msg = "build failed")
    {
        return {false, msg, std::nullopt, nullptr};
    }

    static ProtocolItemBuildResult Success(std::optional<std::string> runtime_key,std::shared_ptr<ProtocolItem> item,  const std::string &msg = "build ok")
    {
        return {true, msg, runtime_key, std::move(item)};
    }

};

struct ParsedSideConfig
{
    virtual ~ParsedSideConfig() = default;

    std::optional<std::string> runtime_key;
};

struct SideConfigParseResult
{
    bool ok{false};
    std::string message;
    std::unique_ptr<ParsedSideConfig> parsed;

    static SideConfigParseResult Failed(const std::string msg = "parse error")
    {
        return {false, msg, nullptr};
    }

    static SideConfigParseResult Success(    std::unique_ptr<ParsedSideConfig> parsed, const std::string msg = "parse ok")
    {
        return {true, msg, std::move(parsed)};
    }
};


class ProtocolSideConfigPolicy;
class ProtocolItemBuildPolicy;

/**
 * @brief 模版方法 + 策略： 整体校验流程骨架不变，不同协议种类、请求/响应侧组合后校验、构建策略不同
 * 
 */
class ProtocolConfigPipeline
{
public:
    static ProtocolConfigCheckResult checkConfig(const ProtocolSideConfigSpec &spec);

    static ProtocolItemBuildResult buildItem(const ProtocolItemBuildSpec &spec);

private:
    static const ProtocolSideConfigPolicy* getSidePolicy(ProtocolType type, ProtocolSide side);
    static const ProtocolItemBuildPolicy* getBuildPolicy(ProtocolType type);
};


class ProtocolSideConfigPolicy
{
public:
    virtual ~ProtocolSideConfigPolicy() = default;

    /**
     * @brief json中出现的字段做类型/格式校验, 必须要完整的配置json
     * @param spec 
     * @return ProtocolConfigCheckResult 
     */
    SideConfigParseResult validateAndParse(const ProtocolSideConfigSpec &spec) const;

protected:
        /**
     * @brief 不同协议种类+不同请求/响应侧允许出现哪些顶层字段
     * @return std::unordered_set<std::string> 
     */
    virtual std::unordered_set<std::string> allowFields() const = 0;

    /**
     * @brief 详细校验配置字段/格式
     * @param spec 
     * @return SideConfigParseResult 
     */
    virtual SideConfigParseResult parseFields(const ProtocolSideConfigSpec &spec) const = 0;

};

struct ParsedHttpReqConfig final: public ParsedSideConfig
{
    HttpItemReqHeaderCfg cfg;
};

class HttpReqPolicy final: public ProtocolSideConfigPolicy
{
public:
    ~HttpReqPolicy() override = default;

protected:
    std::unordered_set<std::string> allowFields() const override { return {"method", "path", "headers"}; }

    SideConfigParseResult parseFields(const ProtocolSideConfigSpec &spec) const override;
};

struct ParsedHttpRespConfig final: public ParsedSideConfig
{
    HttpItemRespHeaderCfg cfg;
};

class HttpRespPolicy final: public ProtocolSideConfigPolicy
{
public:
    ~HttpRespPolicy() override = default;

protected:
    std::unordered_set<std::string> allowFields() const override { return {"status_code", "headers"}; }

    SideConfigParseResult parseFields(const ProtocolSideConfigSpec &spec) const override;
};

struct ParsedCustomTcpConfg final: public ParsedSideConfig
{
    CustomTcpItemCfg cfg;
};


class CustomTcpReqPolicy final: public ProtocolSideConfigPolicy
{
public:
    ~CustomTcpReqPolicy() override = default;

protected:
    std::unordered_set<std::string> allowFields() const override { return {"function_code", "fields"}; }

    SideConfigParseResult parseFields(const ProtocolSideConfigSpec &spec) const override;
};


class CustomTcpRespPolicy final: public ProtocolSideConfigPolicy
{
public:
    ~CustomTcpRespPolicy() override = default;

protected:
    std::unordered_set<std::string> allowFields() const override { return {"function_code", "fields"}; }

    SideConfigParseResult parseFields(const ProtocolSideConfigSpec &spec) const override;
};


class ProtocolItemBuildPolicy
{
public:
    virtual ~ProtocolItemBuildPolicy() = default;

    /**
     * @brief 通过校验过的配置项数据进行ProtocolItem运行态数据进行构建
     * @param spec 
     * @param req 
     * @param resp 
     * @return ProtocolItemBuildResult 
     */
    virtual ProtocolItemBuildResult build(const ProtocolItemBuildSpec &spec,
        const ParsedSideConfig& req,
        const ParsedSideConfig& resp) const = 0;
};


class HttpBuildPolicy final: public ProtocolItemBuildPolicy
{
public:
    ~HttpBuildPolicy() override = default;
    ProtocolItemBuildResult build(const ProtocolItemBuildSpec &spec,
        const ParsedSideConfig& req,
        const ParsedSideConfig& resp) const override;
};

class CustomTcpBuildPolicy final: public ProtocolItemBuildPolicy
{
public:
    ~CustomTcpBuildPolicy() override = default;
    ProtocolItemBuildResult build(const ProtocolItemBuildSpec &spec,
        const ParsedSideConfig& req,
        const ParsedSideConfig& resp) const override;};

}
#endif //__KIT_PROTOCOL_CFG_PIPELINE_H__