/**
 * @file test_websocket_util.cpp
 * @brief websocket辅助工具测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-24 19:18:58
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "../test_log.h"
#include "net/http/http_util.h"
#include "net/websocket/websocket_util.h"
#include <gtest/gtest.h>

using namespace kit_muduo::ws;

/*
测试思路：
1. 使用一个固定的浏览器 Sec-WebSocket-Key，验证服务端按 key + GUID 做 SHA1 后再 Base64。
2. 同时验证同一个 key 能通过 16 字节随机值校验。

示例：
  client key
      |
      v
  SHA1(key + 258EAFA5-E914-47DA-95CA-C5AB0DC85B11)
      |
      v
  Sec-WebSocket-Accept
*/
TEST(TestWebSocketUtil, BuildAndValidateWebSocketAcceptKey)
{
    constexpr const char *client_key = "fTqfLI4bTWpfPC4dS3qcjQ==";
    const auto& accept_key = BuildWebSocketAcceptKey(client_key);
    ASSERT_EQ(accept_key, "ZC0Y8fTGWcb1HW9zfWv0hF33y7E=");

    ASSERT_TRUE(IsValidWebSocketKey(client_key));

}

/*
测试思路：
1. RFC 6455 握手示例给出了公开的 key/accept 对照表。
2. 这里用标准样例防止 GUID、SHA1 字节序、Base64 编码任一环节写错。

示例：
  dGhlIHNhbXBsZSBub25jZQ==
      |
      v
  s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
*/
TEST(TestWebSocketUtil, BuildAcceptKeyMatchesRfc6455Example)
{
    constexpr const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";

    ASSERT_EQ(BuildWebSocketAcceptKey(client_key),
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

/*
测试思路：
1. Sec-WebSocket-Key 允许 header value 两侧有空白，校验前应 trim。
2. 合法 key 必须 Base64 解码成功，并且解码后正好是 16 字节。
3. 空值、非 Base64、解码长度不是 16 字节都应拒绝。

示例：
  " fTqfLI4bTWpfPC4dS3qcjQ== "  -> 16 bytes -> valid
  "aGVsbG8="                   -> 5 bytes  -> invalid
*/
TEST(TestWebSocketUtil, ValidateKeyRequiresBase64Decoded16Bytes)
{
    ASSERT_TRUE(IsValidWebSocketKey(" \t fTqfLI4bTWpfPC4dS3qcjQ== \r\n"));

    ASSERT_FALSE(IsValidWebSocketKey(""));
    ASSERT_FALSE(IsValidWebSocketKey("not base64"));
    ASSERT_FALSE(IsValidWebSocketKey("aGVsbG8="));
}

/*
测试思路：
1. WebSocket Upgrade 校验依赖 HTTP header helper，而 header token/name 都应大小写不敏感。
2. 这里使用单 token，避免把该用例和逗号分隔解析问题耦合在一起。

示例：
  Connection: upgrade
  Upgrade: WebSocket
      |
      v
  token/name 均可命中
*/
TEST(TestWebSocketUtil, HeaderHelpersMatchUpgradeTokenCaseInsensitively)
{
    ASSERT_TRUE(kit_muduo::http::HeaderContainsToken("upgrade", "Upgrade"));
    ASSERT_TRUE(kit_muduo::http::IsHeaderName("WebSocket", "websocket"));
    ASSERT_FALSE(kit_muduo::http::HeaderContainsToken("keep-alive", "Upgrade"));
}
