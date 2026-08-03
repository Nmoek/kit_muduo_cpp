/**
 * @file config_context.cpp
 * @brief 配置错误上下文实现
 */
#include "base/config_context.h"

#include <utility>

namespace kit_muduo {

namespace {

std::string JoinNodePath(
    const std::string& prefix,
    const std::string& path)
{
    if(prefix.empty())
    {
        return path;
    }
    if(path.empty())
    {
        return prefix;
    }
    if(path.front() == '[')
    {
        return prefix + path;
    }
    return prefix + "." + path;
}

std::string BuildErrorMessage(
    const ConfigContext& context,
    const std::string& reason)
{
    std::string message{"config error"};

    if(!context.source.empty())
    {
        message += " in " + context.source;
    }
    if(context.line > 0)
    {
        message += ":" + std::to_string(context.line);
        if(context.column > 0)
        {
            message += ":" + std::to_string(context.column);
        }
    }
    if(!context.node_path.empty())
    {
        message += " at " + context.node_path;
    }
    if(!reason.empty())
    {
        message += ": " + reason;
    }
    return message;
}

} // namespace

ConfigContext ConfigContext::prependNodePath(
    const std::string& prefix) const
{
    ConfigContext context{*this};
    context.node_path = JoinNodePath(prefix, node_path);
    return context;
}

ConfigContext ConfigContext::withSourceIfEmpty(
    const std::string& source) const
{
    ConfigContext context{*this};
    if(context.source.empty())
    {
        context.source = source;
    }
    return context;
}

ConfigContext ConfigContext::withLocationIfEmpty(
    size_t line,
    size_t column) const
{
    ConfigContext context{*this};
    if(context.line == 0)
    {
        context.line = line;
    }
    if(context.column == 0)
    {
        context.column = column;
    }
    return context;
}

ConfigError::ConfigError(
    ConfigContext context,
    std::string reason)
    :std::runtime_error(
        BuildErrorMessage(context, reason))
    ,context_(std::move(context))
    ,reason_(std::move(reason))
{
}

ConfigError ConfigError::prependPath(
    const std::string& prefix) const
{
    return ConfigError(
        context_.prependNodePath(prefix),
        reason_);
}

ConfigError ConfigError::withSourceIfEmpty(
    const std::string& source) const
{
    return ConfigError(
        context_.withSourceIfEmpty(source),
        reason_);
}

ConfigError ConfigError::withFallbackContext(
    const ConfigContext& fallback) const
{
    ConfigContext context{context_};
    if(context.source.empty())
    {
        context.source = fallback.source;
    }
    if(context.node_path.empty())
    {
        context.node_path = fallback.node_path;
    }
    context = context.withLocationIfEmpty(
        fallback.line,
        fallback.column);
    return ConfigError(std::move(context), reason_);
}

} // namespace kit_muduo
