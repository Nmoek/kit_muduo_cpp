/**
 * @file protocol_runtime_key.cpp
 * @brief 协议运行键生成
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-09
 */
#include "domain/protocol_runtime_key.h"

#include "domain/custom_tcp_field_model.h"
#include "net/http/http_request.h"

namespace kit_domain {

namespace {

std::optional<std::string> GetStringField(const nlohmann::json &root, const char *field)
{
    auto it = root.find(field);
    if(it == root.end() || !it.value().is_string())
    {
        return std::nullopt;
    }

    return it.value().get<std::string>();
}

bool IsValidHttpMethod(const std::string &method)
{
    return !method.empty()
        && kit_muduo::http::HttpRequest::Method::kInvaild
            != kit_muduo::http::HttpRequest::Method::FromString(method).toInt();
}

bool IsValidHttpPath(const std::string &path)
{
    return !path.empty() && path[0] == '/';
}

bool IsValidCodeHex(const std::string &code)
{
    if(code.size() <= 1 || code[0] != 'H')
    {
        return false;
    }
    for(int i = 1;i < code.size();++i)
    {
        if(!std::isxdigit(static_cast<unsigned char>(code[i])))
        {
            return false;
        }
    }

    return true;
}

} // namespace

std::optional<std::string> GenerateProtocolRuntimeKey(ProtocolType type, const nlohmann::json &req_cfg)
{
    if(ProtocolType::kHttp == type || ProtocolType::kHttps == type)
    {
        auto method = GetStringField(req_cfg, "method");
        if(!method.has_value() || !IsValidHttpMethod(method.value()))
        {
            return std::nullopt;
        }

        auto path = GetStringField(req_cfg, "path");
        if(!path.has_value() || !IsValidHttpPath(path.value()))
        {
            return std::nullopt;
        }

        const std::string protocol_prefix = ProtocolType::kHttp == type ? "HTTP" : "HTTPS";
        return protocol_prefix + "|" + method.value() + "|" + path.value();
    }
    else if(ProtocolType::kCustomTcp == type)
    {
        auto function_code = GetStringField(req_cfg, "function_code");
        if(!function_code.has_value() || !IsValidCodeHex(function_code.value()))
        {
            return std::nullopt;
        }
        return "TCP|" + function_code.value();
    }

    return std::nullopt;
}

} // namespace kit_domain
