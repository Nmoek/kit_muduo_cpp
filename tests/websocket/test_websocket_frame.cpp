/**
 * @file test_websocket_util.cpp
 * @brief websocket帧数据序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-24 19:18:58
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "net/endian.h"
#include "net/websocket/websocket_frame.h"
#include <gtest/gtest.h>

using namespace kit_muduo;
using namespace kit_muduo::ws;

/*
测试思路：
1. 服务端发送短 text frame 时，payload length 应直接写在第二个字节低 7 位。
2. 服务端 frame 不 mask，FIN 固定为 1，opcode 使用 text=0x01。
3. payload 字节应原样跟在 2 字节 frame header 后面。

示例：
  "hello"
      |
      v
  0x81 0x05 h e l l o
*/
TEST(TestWebSocketFrame, BuildUint8LengthFrame)
{
    const auto& bytes = BuildWebSocketFrameBytes(WebSocketOpcode::kText, "hello");
    ASSERT_EQ(bytes.size(), 7);

    // fin = 1
    EXPECT_EQ(bytes[0] >> 7, 1);
    // opcode = 0x01
    EXPECT_EQ(bytes[0] & 0x0F, 0x01);
    // mask
    EXPECT_EQ(bytes[1] >> 7, 0);
    // <125长度
    EXPECT_EQ(bytes[1] & 0x7F, 5);
    // 负载比较
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 2, bytes.end()), std::vector<uint8_t>({0x68, 0x65, 0x6c, 0x6c, 0x6f}));

}

/*
测试思路：
1. payload 长度等于 126 时，服务端 frame 必须进入 16-bit 扩展长度编码。
2. 扩展长度使用网络大端，即 126 应编码成 0x00 0x7E。
3. 负载区仍从第 4 字节开始原样追加。

示例：
  126 bytes
      |
      v
  0x81 0x7E 0x00 0x7E payload...
*/
TEST(TestWebSocketFrame, BuildUint16LengthFrame)
{
    const auto& bytes = BuildWebSocketFrameBytes(WebSocketOpcode::kText, std::string(126, 'a'));
    ASSERT_EQ(bytes.size(), 4+126);

    // fin = 1
    EXPECT_EQ(bytes[0] >> 7, 1);
    // opcode = 0x01
    EXPECT_EQ(bytes[0] & 0x0F, 0x01);
    // mask
    EXPECT_EQ(bytes[1] >> 7, 0);

    // >125长度
    EXPECT_EQ(bytes[1] & 0x7F, 126);
    // 拓展长度
    EXPECT_EQ(bytes[2], 0x00);
    // 拓展长度
    EXPECT_EQ(bytes[3], 0x7E);
    // 负载比较
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 4, bytes.end()), std::vector<uint8_t>(126, 0x61));

}

/*
测试思路：
1. payload 长度超过 65535 时，服务端 frame 必须进入 64-bit 扩展长度编码。
2. 70000 的网络大端编码为 0x00 0x00 0x00 0x00 0x00 0x01 0x11 0x70。
3. 负载区应从第 10 字节开始，且全部保持原始字节。

示例：
  70000 bytes
      |
      v
  0x81 0x7F 8-byte-length payload...
*/
TEST(TestWebSocketFrame, BuildUint64LengthFrame)
{
    const auto& bytes = BuildWebSocketFrameBytes(WebSocketOpcode::kText, std::string(70000, 'b'));
    ASSERT_EQ(bytes.size(), 2+8+70000);

    // fin = 1
    EXPECT_EQ(bytes[0] >> 7, 1);
    // opcode = 0x01
    EXPECT_EQ(bytes[0] & 0x0F, 0x01);
    // mask
    EXPECT_EQ(bytes[1] >> 7, 0);

    // >125长度
    EXPECT_EQ(bytes[1] & 0x7F, 127);
    // 拓展长度比较
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 2, bytes.begin() + 10), std::vector<uint8_t>({0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x11, 0x70}));
    // 负载比较
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 10, bytes.end()), std::vector<uint8_t>(70000, 0x62));

}

/*
测试思路：
1. 第 3/4 点要求 websocket_frame 只负责网络层 frame 构造，不理解业务字段。
2. binary payload 应使用 opcode=0x02，服务端仍不 mask，payload 原样追加。

示例：
  {0x00, 0xFF, 0x10}
      |
      v
  0x82 0x03 0x00 0xFF 0x10
*/
TEST(TestWebSocketFrame, BuildBinaryFrameKeepsOpcodeAndPayload)
{
    const std::vector<uint8_t> payload{0x00, 0xFF, 0x10};
    const auto &bytes = BuildWebSocketFrameBytes(WebSocketOpcode::kBinary, payload);

    ASSERT_EQ(bytes.size(), 2 + payload.size());
    EXPECT_EQ(bytes[0] >> 7, 1);
    EXPECT_EQ(bytes[0] & 0x0F, 0x02);
    EXPECT_EQ(bytes[1] >> 7, 0);
    EXPECT_EQ(bytes[1] & 0x7F, payload.size());
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 2, bytes.end()), payload);
}

/*
测试思路：
1. 125 是 7-bit payload length 的最大边界，不能提前切到扩展长度。
2. frame 总长度应为 2 字节 header + 125 字节 payload。

示例：
  125 bytes
      |
      v
  0x81 0x7D payload...
*/
TEST(TestWebSocketFrame, BuildUint8LengthFrameBoundary125)
{
    const auto &bytes = BuildWebSocketFrameBytes(WebSocketOpcode::kText, std::string(125, 'x'));

    ASSERT_EQ(bytes.size(), 2 + 125);
    EXPECT_EQ(bytes[1] >> 7, 0);
    EXPECT_EQ(bytes[1] & 0x7F, 125);
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 2, bytes.end()), std::vector<uint8_t>(125, 'x'));
}

/*
测试思路：
1. 65535 是 16-bit 扩展长度的最大边界，仍应使用 length=126 格式。
2. 扩展长度字段应写成网络大端 0xFF 0xFF。

示例：
  65535 bytes
      |
      v
  0x81 0x7E 0xFF 0xFF payload...
*/
TEST(TestWebSocketFrame, BuildUint16LengthFrameBoundary65535)
{
    const auto &bytes = BuildWebSocketFrameBytes(WebSocketOpcode::kText, std::string(65535, 'y'));

    ASSERT_EQ(bytes.size(), 4 + 65535);
    EXPECT_EQ(bytes[1] >> 7, 0);
    EXPECT_EQ(bytes[1] & 0x7F, 126);
    EXPECT_EQ(bytes[2], 0xFF);
    EXPECT_EQ(bytes[3], 0xFF);
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 4, bytes.end()), std::vector<uint8_t>(65535, 'y'));
}

/*
测试思路：
1. close payload 前两个字节必须是 close code 的网络大端编码。
2. reason 文本跟在 code 后面，当前实现按原始字节追加。

示例：
  1000 + "hello"
      |
      v
  0x03 0xE8 h e l l o
*/
TEST(TestWebSocketFrame, BuildClosePayload)
{
    const auto &bytes = BuildClosePayload(CloseCode::kNormalShutdown, "hello");
    ASSERT_EQ(bytes.size(), 7);

    // code
    EXPECT_EQ(bytes[0], 0x03);
    EXPECT_EQ(bytes[1], 0xE8);
    // 原因文本utf-8
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 2, bytes.end()), std::vector<uint8_t>({0x68, 0x65, 0x6c, 0x6c, 0x6f}));
}

/*
测试思路：
1. WebSocket control frame payload 最大 125 字节。
2. close payload 需要预留 2 字节 close code，因此 reason 最多保留 123 字节。
3. 超长 reason 应被截断，避免构造出非法 close frame。

示例：
  1009 + 200 bytes reason
      |
      v
  2-byte code + first 123 bytes reason
*/
TEST(TestWebSocketFrame, BuildClosePayloadTruncatesReasonToControlFrameLimit)
{
    const std::string reason(200, 'z');
    const auto &bytes = BuildClosePayload(CloseCode::kMessageLarge, reason);

    ASSERT_EQ(bytes.size(), 125);
    EXPECT_EQ(bytes[0], 0x03);
    EXPECT_EQ(bytes[1], 0xF1);
    EXPECT_EQ(std::vector<uint8_t>(bytes.begin() + 2, bytes.end()), std::vector<uint8_t>(123, 'z'));
}
