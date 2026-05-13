/**
 * @file runtime_result.cpp
 * @brief 业务运行态
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-11 10:24:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/runtime_result.h"


namespace kit_domain {


const std::unordered_map<int32_t, std::string> RuntimeError::s_error_msg{
    {kOk, "success"},

    /* invoke  */
    {kInvokeInvalidArgument, "invoke arguments invalid"},
    {kInvokeTimeout, "invoke timeout"},
    {kInvokeDeferred, "invoke deffered"},
    {kInvokeException, "invoke exception"},

    /* domain runtime */
    {kInvalidArgument, "input arguments invalid"},
    {kNullProtocolItem, "protocol item is null"},
    {kProtocolItemNotFound, "protocol item not found"},
    {kProtocolTypeMismatch, "protocol type mismatch"},
    {kInvalidProtocolConfig, "invalid protocol config"},
    {kRouteNotFound, "route not found"},
    {kRouteConflict, "route conflict"},
    {kFuncCodeNotFound, "func code not found"},
    {kFuncCodeConflict, "func code conflict"},

    {kInternalError, "internal error"}
};

}