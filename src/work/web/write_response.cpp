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
#include "net/http/http_content.h"
#include "net/http/http_context.h"
#include "net/http/http_response.h"

namespace kit_domain {


namespace {
inline static int32_t RuntimeControlCodeToWriteCode(RuntimeControlCode code)
{
    switch (code) 
    {
        case RuntimeControlCode::kOk:
        {
            return 0;
        }
        case RuntimeControlCode::kInvalidArgument:
        case RuntimeControlCode::kProjectTypeInvalid:
        {
            return -200;
        }

        default:
        {
            return -300;
        }
    
    }
}

}


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


WriteOpResult WriteOpResult::FromPjRuntimeResult(ProjectRuntimeResult pj_result)
{
    WriteOpResult write_result;

    write_result.persisted = pj_result.receipt.persisted;
    write_result.runtime_applied = pj_result.receipt.runtime_applied;

    if(!pj_result.ok())
    {
        return write_result.failed(RuntimeControlCodeToWriteCode(pj_result.status.code), pj_result.status.message);
    }

    return write_result.success(pj_result.status.message);

}

WriteOpResult WriteOpResult::FromPcRuntimeResult(ProtocolRuntimeResult pc_result)
{
    WriteOpResult write_result;

    write_result.persisted = pc_result.receipt.persisted;
    write_result.runtime_applied = pc_result.receipt.runtime_applied;

    if(!pc_result.ok())
    {
        return write_result.failed(RuntimeControlCodeToWriteCode(pc_result.status.code), pc_result.status.message);
    }

    return write_result.success(pc_result.status.message);

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

    ctx->response()->setJson(root);
}

}
