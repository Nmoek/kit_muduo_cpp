/**
 * @file websocket_utils.h
 * @brief websocket辅助工具
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-24 11:47:45
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WEBOSCKET_UTILS_H__
#define __KIT_WEBOSCKET_UTILS_H__

#include "net/call_backs.h"
#include <string>

namespace kit_muduo::ws {

/**
 * @brief 构建Upgrade握手响应AcceptKey
 * @param client_key 
 * @return std::string 
 */
std::string BuildWebSocketAcceptKey(const std::string &client_key);


/**
 * @brief 校验Sec-WebSocket-Key 必须是一个 base64 字符串，base64 解码后必须正好是 16 字节随机值。
 * @param key 
 * @return true 
 * @return false 
 */
bool IsValidWebSocketKey(const std::string &key);



}
#endif // __KIT_WEBOSCKET_UTILS_H__