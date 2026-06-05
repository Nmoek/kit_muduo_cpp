/**
 * @file write_response.cpp
 * @brief  HTTP写操作响应
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-03 19:25:42
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "web/write_response.h"
#include "nlohmann/json.hpp"
#include "net/http/http_context.h"
#include "net/http/http_response.h"

namespace kit_domain {


WriteOpResult& WriteOpResult::allOk()
{
    this->persisted = this->runtime_applied = 1;
    return *this;
}

WriteOpResult& WriteOpResult::allErr()
{
    this->persisted = this->runtime_applied = 0;
    return *this;
}

WriteOpResult& WriteOpResult::persistedOk()
{
    this->persisted = 1;
    return *this;
}
WriteOpResult& WriteOpResult::runOk()
{
    this->runtime_applied = 1;
    return *this;
}

WriteOpResult& WriteOpResult::success(const std::string &msg)
{
    this->code = 0;
    this->message = msg.empty() ? "success" : msg;

    return *this;
}

WriteOpResult& WriteOpResult::failed(int32_t code, const std::string &msg)
{
    this->code = code == 0 ? -300 : code;
    this->message = msg.empty() ? "write operation failed" : msg;

    return *this;
}


void WriteOpResponseHelper(kit_muduo::HttpContextPtr ctx, const WriteOpResult &result, WriteOpDataFunc func)
{
    nlohmann::json root = nlohmann::json::object();

    root["code"] = result.code;
    root["message"] = result.message;
    root["data"] = nlohmann::json::object();
    root["data"]["persisted"] = result.persisted;
    root["data"]["runtime_applied"] = result.runtime_applied;

    if(func)
    {
        func(root);
    }

    ctx->response()->body().appendData(root.dump());
}

}