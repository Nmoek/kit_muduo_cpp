/**
 * @file config.cpp
 * @brief 配置系统
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-31 18:37:13
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/config.h"
#include <cctype>
#include <stdexcept>
#include <fstream>

namespace kit_muduo {



ConfigPreparedUpdate::ConfigPreparedUpdate(std::function<void()> commit)
    :commit_cb_(std::move(commit))
    ,committed_(false)
{
    if(!commit_cb_)
    {
        throw std::invalid_argument( "prepared update commit must not be empty");
    }

}

ConfigPreparedUpdate::ConfigPreparedUpdate(ConfigPreparedUpdate&& other) noexcept
    :commit_cb_(std::move(other.commit_cb_))
    ,committed_(other.committed_)
{
    other.committed_ = true;
}

ConfigPreparedUpdate& ConfigPreparedUpdate::operator=(ConfigPreparedUpdate&& other) noexcept
{
    if(this == &other)
    {
        return *this;
    }
    commit_cb_ = std::move(other.commit_cb_);
    other.commit_cb_ = nullptr;
    committed_ = other.committed_;
    other.committed_ = true;
    return *this;
}



void ConfigPreparedUpdate::commit() noexcept
{
    if(!committed_ && commit_cb_)
    {
        commit_cb_();
        committed_ = true;
    }
}

ConfigContext RegistryContext(const std::string& node_path)
{
    ConfigContext context;
    context.source = "registry";
    context.node_path = node_path;
    return context;
}

bool CheckConfigPathSegment(const std::string &segment)
{
    if(segment.empty())
    {
        return false;
    }

    for(const unsigned char value : segment)
    {
        const bool is_ascii_letter = (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z');
        const bool is_digit = value >= '0' && value <= '9';
        if(!is_ascii_letter && !is_digit && value != '_')
        {
            return false;
        }
    }
    return true;
}

std::string NormalizeConfigPathSegment(const std::string& segment)
{
    if(segment.empty())
    {
        throw ConfigError(RegistryContext(segment),
            "empty configuration path segment");
    }

    std::string result;
    result.reserve(segment.size());
    for(const unsigned char value : segment)
    {
        if(value >= 'A' && value <= 'Z')
        {
            result.push_back(static_cast<char>(value - 'A' + 'a'));
        }
        else
        {
            result.push_back(static_cast<char>(value));
        }
    }

    if(!CheckConfigPathSegment(result))
    {
        throw ConfigError(RegistryContext(segment),
            "configuration path segment contains an invalid character");
    }
    return result;
}


std::string NormalizeFullConfigPath(const std::string& node_path)
{
    std::vector<std::string> parts = SplitConfigNodePath(node_path);

    if(parts.empty())
    {
        throw ConfigError(RegistryContext(node_path),
            "configuration path must not be empty");
    }

    for(auto& part : parts)
    {
        if(part.empty())
        {
            throw ConfigError(RegistryContext(node_path),
                "configuration path contains an empty segment");
        }

        part = NormalizeConfigPathSegment(part);
    }

    std::string result;
    for(const auto& child_path : parts)
    {
        result = JoinConfigPath(result, child_path);
    }
    return result;
}

std::vector<std::string> SplitConfigNodePath(const std::string& node_path)
{
    if(node_path.empty())
    {
        throw ConfigError(RegistryContext(node_path),
            "configuration path is empty");
    }

    std::vector<std::string> result;
    size_t begin = 0;
    while(begin <= node_path.size())
    {
        const size_t end = node_path.find('.', begin);
        const std::string raw = node_path.substr(begin, 
            end == std::string::npos ? std::string::npos : end - begin);

        result.push_back(NormalizeConfigPathSegment(raw));

        if(end == std::string::npos) 
        {
            break;
        }
        begin = end + 1;
    }
    return result;
}

std::string SpliceConfigNodePath(const std::string& node_path)
{
    std::string result;
    for(const auto& child_path : SplitConfigNodePath(node_path))
    {
        result = JoinConfigPath(result, child_path);
    }
    return result;
}

std::string JoinConfigPath(const std::string& prefix, const std::string& node_path)
{
    return prefix.empty() ? node_path : prefix + "." + node_path;
}

bool IsAncestorConfigPath(const std::string& ancestor, const std::string& descendant)
{
    return descendant.size() > ancestor.size()
        && descendant.compare(0, ancestor.size(), ancestor) == 0
        && descendant[ancestor.size()] == '.';
}

std::string ReadConfigFile(const std::string &file_path)
{
    std::fstream f(file_path);
    if(!f.is_open())
    {
        std::error_code ec(errno, std::generic_category());

        throw ConfigError(
            ConfigContext{
                .source = file_path
            },
            "cannot read config file: " + ec.message());
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if(f.bad())
    {
        throw ConfigError(
            ConfigContext{
                .source = file_path
            },
            "cannot read config file");
    }
    return ss.str();
}



}
