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
#include "stduuid/uuid.h"

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

std::string Sha1BytesHelper(const std::string &data)
{
    uuids::detail::sha1 sha;
    sha.process_bytes(data.data(), data.size());


    uuids::detail::sha1::digest8_t digest;
    sha.get_digest_bytes(digest);

    return std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
}

} // namesapce

std::string BuildWebSocketAcceptKey(const std::string &client_key)
{
    if(client_key.empty())
    {
        return "";
    }
    const std::string &digest_str = Sha1BytesHelper(client_key + kWebSocketGuid);

    return cppcodec::base64_rfc4648::encode(reinterpret_cast<const uint8_t*>(digest_str.data()), digest_str.size());
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