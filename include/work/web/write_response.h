/**
 * @file write_response.h
 * @brief HTTP写操作响应
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-03 19:02:03
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WRITE_RESPONSE_H__
#define __KIT_WRITE_RESPONSE_H__

#include "net/call_backs.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <string>
#include <functional>

namespace kit_domain {


using WriteOpDataFunc = std::function<void(nlohmann::json&)>;

struct WriteOpResult
{
    int32_t code{0};
    int32_t persisted{0};
    int32_t runtime_applied{0};
    std::string message;

    WriteOpResult& allOk();
    WriteOpResult& allErr();
    WriteOpResult& persistedOk();
    WriteOpResult& runOk();

    WriteOpResult& success(const std::string &msg = "success");

    WriteOpResult& failed(int32_t code,  const std::string &msg = "write operation failed");

};

void WriteOpResponseHelper(kit_muduo::HttpContextPtr ctx, const WriteOpResult &result, WriteOpDataFunc func = WriteOpDataFunc());


}
#endif //__KIT_WRITE_RESPONSE_H__