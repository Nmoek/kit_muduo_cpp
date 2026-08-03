/**
 * @file config_context.h
 * @brief 配置系统上下文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-31 16:04:07
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CONFIG_CONTEXT_H__
#define __KIT_CONFIG_CONTEXT_H__

#include <cstddef>
#include <stdexcept>
#include <string>

namespace kit_muduo {


struct ConfigContext
{
    std::string source;
    std::string node_path;
    size_t line{0};
    size_t column{0};

    ConfigContext prependNodePath(const std::string& prefix) const;
    ConfigContext withSourceIfEmpty(const std::string& source) const;
    ConfigContext withLocationIfEmpty(
        size_t line,
        size_t column) const;
};


class ConfigError: public std::runtime_error
{
public:
    ConfigError(ConfigContext context, std::string reason);

    /**
     * @brief 追加错误信息
     * @param prefix 
     * @return ConfigError 
     */
    ConfigError prependPath(const std::string& prefix) const;
    ConfigError withSourceIfEmpty(
        const std::string& source) const;
    ConfigError withFallbackContext(
        const ConfigContext& fallback) const;

    const ConfigContext& context() const noexcept { return context_; }
    
    const std::string& reason() const noexcept { return reason_; }

private:
    ConfigContext context_;
    std::string reason_;
};
}
#endif //__KIT_CONFIG_CONTEXT_H__ 
