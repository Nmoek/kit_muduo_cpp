/**
 * @file http_util.cpp
 * @brief 
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-31 20:20:14
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/http/http_util.h"

namespace kit_muduo::http {

std::unordered_map<int32_t, std::string> StateCode::s_m_codeMessageMap{
    {kUnknow, ""},
    {k200Ok,                         "OK"},
    {k204NoContent,                  "No Content"},
    {k301MovedPermanently,           "Moved Permanently"},
    {k302MoveTemporarily,            "Move temporarily"},
    {k400BadRequest,                 "Bad Request"},
    {k403Forbidden,                  "Forbidden"},
    {k404NotFound,                   "Not Found"},
    {k454SessionNotFound, "Session Not Found"},
    {k455MethodNotValid,             "Method Not Valid"},
    {k500InternalServerError,        "Internal Server Error"},
    {k503ServicUnavailable, "Servic Unavailable"}
};


}