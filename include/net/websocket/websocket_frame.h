/**
 * @file websocket_frame.h
 * @brief websocket 帧数据序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-24 14:23:24
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_WEBSOCKET_FRAME_H__
#define __KIT_WEBSOCKET_FRAME_H__

#include "base/time_stamp.h"

#include <cstdint>
#include <string>
#include <vector>


namespace kit_muduo::ws {

enum class WebSocketOpcode: uint8_t 
{
    kContinuation   = 0x00, //继续帧p
    kText           = 0x01, //文本帧
    kBinary         = 0x02, // 二进制帧
    kClose          = 0x08, // 关闭帧
    kPing           = 0x09, // 心跳请求帧
    kPong           = 0x0A, // 心跳响应帧
};


/*
byte0 = FIN(1) + RSV(3) + opcode(4)
byte1 = MASK(0) + payload length(7)
    length <= 125: 直接写长度
2bytes    126: 后跟 2 字节网络序长度
4bytes    127: 后跟 8 字节网络序长度
4bytes    MASK=1 掩码值
payload 原样追加
*/

struct WebSocketFrame
{
    /// @brief 是否是最后一帧 注意:V1版本默认所有帧都是最后一帧
    bool fin{true};
    /// @brief 操作帧类型 默认文本帧
    WebSocketOpcode opcode{WebSocketOpcode::kText};
    kit_muduo::TimeStamp receive_time{};
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> BuildWebSocketFrameBytes(WebSocketOpcode opcode, const std::vector<uint8_t> &payload);

std::vector<uint8_t> BuildWebSocketFrameBytes(WebSocketOpcode opcode, const std::string &payload);

/*
关闭帧负载格式:
┌───────────┬──────────────┐
│  状态码    │  关闭原因    │
│  (2字节)   │  (UTF-8文本) │
└───────────┴──────────────┘

常用状态码:
1000: 正常关闭
1001: 端点离开 (如页面导航)
1002: 协议错误
1003: 不支持的数据类型
1005: 无状态码 (保留)
1006: 异常关闭 (保留)
1007: 数据格式不一致
1008: 策略违反
1009: 消息过大
1010: 缺少扩展
1011: 服务器错误
1015: TLS握手失败 (保留)

示例:
关闭帧: [0x88, 0x0A, 0x03, 0xE8, 0x48, 0x65, 0x6C, 0x6C, 0x6F]
解析:
  0x88: FIN=1, Opcode=8 (Close)
  0x0A: MASK=0, Length=10
  0x03, 0xE8: 状态码 1000
  0x48, 0x65, 0x6C, 0x6C, 0x6F: "Hello"
*/


enum class CloseCode: uint16_t
{
    kNormalShutdown         = 1000,
    kEndpointDeparture      = 1001,
    kProtocolError          = 1002,
    kUnsupportedDataType    = 1003,
    kNoStatusCode           = 1005,
    kExceptionalShutdown    = 1006,
    kInconsistentFormat     = 1007,
    kPolicyInvalid          = 1008,
    kMessageLarge           = 1009,
    kMissingExtension       = 1010,
    kServerError            = 1011,
    kTLSFailure             = 1015,
};

/**
 * @brief 构建关闭帧负载数据(注意 这是标准格式)
 * @param code 具体关闭代码
 * @param reason 具体关闭原因字符串
 * @return std::vector<uint8_t> 
 */
std::vector<uint8_t> BuildClosePayload(CloseCode code, const std::string &reason);




}
#endif //__KIT_WEBSOCKET_FRAME_H__