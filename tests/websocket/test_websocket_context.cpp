/**
 * @file test_websocket_context.cpp
 * @brief websocket客户端帧解析上下文测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-30 00:00:00
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/websocket/websocket_context.h"
#include "net/websocket/websocket_frame.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace kit_muduo;
using namespace kit_muduo::ws;

namespace {

std::vector<uint8_t> ToBytes(const std::string &text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

void AppendUint16BE(std::vector<uint8_t> *out, uint16_t value)
{
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendUint64BE(std::vector<uint8_t> *out, uint64_t value)
{
    for(int i = 7; i >= 0; --i)
    {
        out->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

std::vector<uint8_t> BuildClientFrame(WebSocketOpcode opcode,
    const std::vector<uint8_t> &payload,
    bool fin = true,
    bool masked = true,
    uint8_t rsv_bits = 0)
{
    const std::vector<uint8_t> mask{0x12, 0x34, 0x56, 0x78};
    std::vector<uint8_t> out;

    uint8_t byte0 = static_cast<uint8_t>(rsv_bits & 0x70);
    if(fin)
    {
        byte0 |= 0x80;
    }
    byte0 |= (static_cast<uint8_t>(opcode) & 0x0F);
    out.push_back(byte0);

    uint8_t byte1 = masked ? 0x80 : 0x00;
    if(payload.size() <= 125)
    {
        out.push_back(byte1 | static_cast<uint8_t>(payload.size()));
    }
    else if(payload.size() <= 0xFFFF)
    {
        out.push_back(byte1 | 126);
        AppendUint16BE(&out, static_cast<uint16_t>(payload.size()));
    }
    else
    {
        out.push_back(byte1 | 127);
        AppendUint64BE(&out, static_cast<uint64_t>(payload.size()));
    }

    if(masked)
    {
        out.insert(out.end(), mask.begin(), mask.end());
        for(size_t i = 0; i < payload.size(); ++i)
        {
            out.push_back(payload[i] ^ mask[i % mask.size()]);
        }
    }
    else
    {
        out.insert(out.end(), payload.begin(), payload.end());
    }

    return out;
}

void AppendToBuffer(Buffer *buf, const std::vector<uint8_t> &bytes)
{
    buf->append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

WebSocketParseResult ParseBytes(WebSocketContext *ctx,
    Buffer *buf,
    const std::vector<uint8_t> &bytes)
{
    AppendToBuffer(buf, bytes);
    return ctx->parseFrame(*buf, TimeStamp(123456));
}

} // namespace

/*
测试思路：
1. 浏览器发来的 text frame 必须带 mask，WebSocketContext 负责解 mask。
2. 成功解析后应返回 kOk、opcode=text、FIN=1、payload 为原始文本。
3. 当前 frame 被完整消费，buffer 不应残留已解析数据。

示例：
  masked "hello"
      |
      v
  parseFrame -> payload "hello", buffer empty
*/
TEST(TestWebSocketContext, ParseMaskedTextFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf, BuildClientFrame(WebSocketOpcode::kText, ToBytes("hello")));

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.frame.fin);
    EXPECT_EQ(result.frame.opcode, WebSocketOpcode::kText);
    EXPECT_EQ(result.frame.payload, ToBytes("hello"));
    EXPECT_EQ(result.frame.receive_time.millSeconds(), 123456);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. 当 buffer 中连续放入两个完整 frame 时，parseFrame 一次只消费一个 frame。
2. 第一次解析后 buffer 应保留第二个 frame；第二次解析再消费剩余数据。

示例：
  frame("one") + frame("two")
      |
      v
  parse -> "one" remains frame("two")
  parse -> "two" remains empty
*/
TEST(TestWebSocketContext, ParseConsumesOnlyOneFrameAtATime)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    const auto first = BuildClientFrame(WebSocketOpcode::kText, ToBytes("one"));
    const auto second = BuildClientFrame(WebSocketOpcode::kText, ToBytes("two"));
    AppendToBuffer(&buf, first);
    AppendToBuffer(&buf, second);

    auto result1 = ctx.parseFrame(buf, TimeStamp(1));
    ASSERT_TRUE(result1.ok());
    EXPECT_EQ(result1.frame.payload, ToBytes("one"));
    EXPECT_EQ(buf.readableBytes(), second.size());

    auto result2 = ctx.parseFrame(buf, TimeStamp(2));
    ASSERT_TRUE(result2.ok());
    EXPECT_EQ(result2.frame.payload, ToBytes("two"));
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. 数据不足时 parseFrame 应返回 kNeedMore，且不能消费 buffer。
2. 当后续数据补齐后，同一个 buffer 应能成功解析出完整消息。

示例：
  first 3 bytes of frame
      |
      v
  need more, readableBytes unchanged
      |
      v
  append rest -> ok
*/
TEST(TestWebSocketContext, NeedMoreDoesNotConsumePartialFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;
    const auto frame = BuildClientFrame(WebSocketOpcode::kText, ToBytes("partial"));

    AppendToBuffer(&buf, std::vector<uint8_t>(frame.begin(), frame.begin() + 3));
    auto partial = ctx.parseFrame(buf, TimeStamp(1));
    ASSERT_TRUE(partial.needMore());
    ASSERT_EQ(buf.readableBytes(), 3u);

    AppendToBuffer(&buf, std::vector<uint8_t>(frame.begin() + 3, frame.end()));
    auto complete = ctx.parseFrame(buf, TimeStamp(2));
    ASSERT_TRUE(complete.ok());
    EXPECT_EQ(complete.frame.payload, ToBytes("partial"));
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. payload 长度为 126 时，客户端 frame 使用 16-bit 扩展长度。
2. 解析器应按网络大端读取扩展长度并解 mask 得到完整 payload。

示例：
  126 bytes masked text
      |
      v
  parseFrame -> 126 bytes original text
*/
TEST(TestWebSocketContext, ParseUint16LengthTextFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;
    const std::vector<uint8_t> payload(126, 'a');

    auto result = ParseBytes(&ctx, &buf, BuildClientFrame(WebSocketOpcode::kText, payload));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.frame.opcode, WebSocketOpcode::kText);
    EXPECT_EQ(result.frame.payload, payload);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. payload 长度超过 65535 时，客户端 frame 使用 64-bit 扩展长度。
2. 解析器应正确读取 8 字节网络大端长度并消费完整 frame。

示例：
  70000 bytes masked text
      |
      v
  parseFrame -> 70000 bytes original text
*/
TEST(TestWebSocketContext, ParseUint64LengthTextFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;
    const std::vector<uint8_t> payload(70000, 'b');

    auto result = ParseBytes(&ctx, &buf, BuildClientFrame(WebSocketOpcode::kText, payload));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.frame.opcode, WebSocketOpcode::kText);
    EXPECT_EQ(result.frame.payload, payload);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

/*
测试思路：
1. WebSocket 客户端到服务端的 frame 必须 mask。
2. 未 mask 的客户端 frame 属于协议错误，解析器应返回 1002。

示例：
  unmasked text frame
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectUnmaskedClientFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kText, ToBytes("hello"), true, false));

    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.needMore());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. v1 不支持 RSV 扩展位，任何 RSV 非 0 的 frame 都应拒绝。
2. 拒绝时返回协议错误 1002。

示例：
  RSV1=1 + masked text
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectRsvBits)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kText, ToBytes("hello"), true, true, 0x40));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. WebSocket opcode 只允许 continuation/text/binary/close/ping/pong。
2. 未定义 opcode 应被识别为协议错误。

示例：
  opcode=0x03
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectUnsupportedOpcode)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(static_cast<WebSocketOpcode>(0x03), ToBytes("bad")));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. v1 不支持分片重组，非控制帧 FIN=0 应直接拒绝。
2. text frame 分片属于当前版本的协议错误路径。

示例：
  FIN=0 text
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectFragmentedTextFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kText, ToBytes("hello"), false));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. v1 不支持 continuation 聚合，因此 continuation frame 不应进入业务层。
2. 解析器应返回协议错误。

示例：
  continuation frame
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectContinuationFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kContinuation, ToBytes("hello")));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. v1 只接收浏览器 text 控制消息，不接受客户端 binary frame。
2. binary frame 应返回 unsupported data type 1003。

示例：
  masked binary
      |
      v
  kError + kUnsupportedDataType
*/
TEST(TestWebSocketContext, RejectClientBinaryFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kBinary, std::vector<uint8_t>{0x01, 0x02}));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kUnsupportedDataType);
}

/*
测试思路：
1. 控制帧不能分片，FIN 必须为 1。
2. 分片 ping frame 应返回协议错误。

示例：
  FIN=0 ping
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectFragmentedControlFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kPing, ToBytes("ping"), false));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. 控制帧 payload 最大 125 字节，不能使用扩展长度承载更大 payload。
2. 超长 ping frame 应返回协议错误。

示例：
  ping payload 126 bytes
      |
      v
  kError + kProtocolError
*/
TEST(TestWebSocketContext, RejectOversizedControlFrame)
{
    WebSocketContext ctx(nullptr);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kPing, std::vector<uint8_t>(126, 'p')));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kProtocolError);
}

/*
测试思路：
1. WebSocketContext 构造参数支持设置单帧最大 payload。
2. payload 超过该限制时应返回 1009 message too large。

示例：
  max payload = 4
  text payload = 5
      |
      v
  kError + kMessageLarge
*/
TEST(TestWebSocketContext, RejectFrameLargerThanConfiguredLimit)
{
    WebSocketContext ctx(nullptr, 4);
    Buffer buf;

    auto result = ParseBytes(&ctx, &buf,
        BuildClientFrame(WebSocketOpcode::kText, ToBytes("12345")));

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.close_code, CloseCode::kMessageLarge);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
