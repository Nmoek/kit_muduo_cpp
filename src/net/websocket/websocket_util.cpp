/**
 * @file websocket_utils.cpp
 * @brief websocket辅助工具
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-24 11:57:41
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/net_log.h"
#include "net/websocket/websocket_util.h"


#include <algorithm>
#include "cppcodec/base64_rfc4648.hpp"
#include "cppcodec/parse_error.hpp"

namespace kit_muduo::ws {


namespace {

constexpr const char *kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string Trim(std::string value)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}



} // namesapce

std::string BuildWebSocketAcceptKey(const std::string &client_key)
{
    if(client_key.empty())
    {
        return "";
    }
    
    return Sha1BytesBase64Helper(client_key + kWebSocketGuid);
}


bool IsValidWebSocketKey(const std::string &key)
{
    const std::string &trim_key = Trim(key);
    if(trim_key.empty())
    {
        return false;
    }

    try {
        const auto decode_key = cppcodec::base64_rfc4648::decode(trim_key);
        // 注意 这里必须是16的长度
        return decode_key.size() == 16;
    } catch(const cppcodec::parse_error &e) {

        WS_F_ERROR("websocket base64 decode error: %s\n", e.what());
        return false;
    }

}









}