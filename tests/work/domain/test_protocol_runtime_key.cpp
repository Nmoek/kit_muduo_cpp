/**
 * @file test_protocol_runtime_key.cpp
 * @brief 协议运行键生成测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-09
 */

#include "domain/protocol_runtime_key.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

#include <optional>
#include <string>

using namespace kit_domain;
using nljson = nlohmann::json;

namespace {

void ExpectRuntimeKey(ProtocolType type, const nljson &req_cfg, const std::string &expected)
{
    const auto key = GenerateProtocolRuntimeKey(type, req_cfg);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key.value(), expected);
}

void ExpectNoRuntimeKey(ProtocolType type, const nljson &req_cfg)
{
    std::optional<std::string> key;
    EXPECT_NO_THROW(key = GenerateProtocolRuntimeKey(type, req_cfg));
    EXPECT_FALSE(key.has_value());
}

} // namespace

/**
 * 测试思路：
 * 1. HTTP 协议运行键由协议类型、请求 method、请求 path 共同决定。
 * 2. 生成格式固定为 HTTP|<METHOD>|<PATH>，不带首尾多余分隔符。
 * 3. method/path 之外的字段不参与运行键，避免展示字段或响应配置影响路由唯一性。
 *
 * 示例：
 *
 *   req_cfg = {"method":"GET","path":"/api/users","headers":{"X-Test":"1"}}
 *        |
 *        v
 *   HTTP|GET|/api/users
 */
TEST(TestProtocolRuntimeKey, HttpRequestCfgGeneratesStableRuntimeKey)
{
    const nljson req_cfg = {
        {"method", "GET"},
        {"path", "/api/users"},
        {"headers", {{"X-Test", "1"}}},
    };

    ExpectRuntimeKey(ProtocolType::kHttp, req_cfg, "HTTP|GET|/api/users");
}

/**
 * 测试思路：
 * 1. HTTPS 和 HTTP 都按 method/path 做路由冲突判断。
 * 2. 当前实现把 HTTPS 的协议前缀生成为 HTTPS，避免和 HTTP 协议项混成同一个 key。
 *
 * 示例：
 *
 *   HTTPS + POST + /secure/login
 *        |
 *        v
 *   HTTPS|POST|/secure/login
 */
TEST(TestProtocolRuntimeKey, HttpsRequestCfgGeneratesStableRuntimeKey)
{
    const nljson req_cfg = {
        {"method", "POST"},
        {"path", "/secure/login"},
    };

    ExpectRuntimeKey(ProtocolType::kHttps, req_cfg, "HTTPS|POST|/secure/login");
}

/**
 * 测试思路：
 * 1. Custom TCP 协议运行键由 function_code 决定。
 * 2. function_code 必须是项目 TCP 格式约定的 H 开头十六进制字符串。
 * 3. 生成格式固定为 TCP|<FUNCTION_CODE>。
 * 4. 其它 common 字段不参与运行键，避免同一功能码被配置成多个可运行协议。
 *
 * 示例：
 *
 *   req_cfg = {"function_code":"H1001","sequence":"H01"}
 *        |
 *        v
 *   TCP|H1001
 */
TEST(TestProtocolRuntimeKey, CustomTcpRequestCfgGeneratesStableRuntimeKey)
{
    const nljson req_cfg = {
        {"function_code", "H1001"},
        {"sequence", "H01"},
    };

    ExpectRuntimeKey(ProtocolType::kCustomTcp, req_cfg, "TCP|H1001");
}

/**
 * 测试思路：
 * 1. HTTP/HTTPS 运行键必须同时具备 method 和 path。
 * 2. 缺少任一字段都不能生成运行键，应返回 nullopt。
 * 3. 这样 manager/repository 可以在写 DB 前识别“请求配置不完整”，而不是等唯一索引或 runtime 失败。
 *
 * 示例：
 *
 *   {"path":"/missing-method"}  -> nullopt
 *   {"method":"GET"}            -> nullopt
 */
TEST(TestProtocolRuntimeKey, HttpRequestCfgMissingRequiredFieldReturnsNullopt)
{
    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"path", "/missing-method"},
    });

    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", "GET"},
    });
}

/**
 * 测试思路：
 * 1. HTTP method 必须是当前 HttpRequest 支持的合法方法。
 * 2. path 必须是以 / 开头的非空路径。
 * 3. 非法 method/path 不能被拼成 runtime_key，否则会污染 DB 唯一索引。
 *
 * 示例：
 *
 *   method=PATCH, path=/api      -> nullopt
 *   method=GET,   path=api       -> nullopt
 *   method=GET,   path=""        -> nullopt
 */
TEST(TestProtocolRuntimeKey, HttpRequestCfgInvalidMethodOrPathReturnsNullopt)
{
    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", "PATCH"},
        {"path", "/api"},
    });

    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", "GET"},
        {"path", "api"},
    });

    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", "GET"},
        {"path", ""},
    });
}

/**
 * 测试思路：
 * 1. Custom TCP 运行键必须具备 function_code。
 * 2. function_code 为空时也不能生成运行键。
 * 3. 这样 kOff/kOn 的唯一索引不会被空 key 误占用。
 *
 * 示例：
 *
 *   {}                         -> nullopt
 *   {"function_code":""}       -> nullopt
 */
TEST(TestProtocolRuntimeKey, CustomTcpMissingOrEmptyFunctionCodeReturnsNullopt)
{
    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, nljson::object());

    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, {
        {"function_code", ""},
    });
}

/**
 * 测试思路：
 * 1. TCP 格式和字段值配置统一使用 H 开头的十六进制字符串。
 * 2. function_code 缺少 H 前缀、只有 H 没有十六进制内容、或包含非十六进制字符时，
 *    都不能生成运行键。
 * 3. 这样 repository/manager 不会把不可被 CustomTcpPattern 解析的功能码写进唯一索引。
 *
 * 示例：
 *
 *   {"function_code":"1001"}    -> nullopt
 *   {"function_code":"H"}       -> nullopt
 *   {"function_code":"H12G4"}   -> nullopt
 *   {"function_code":"h1001"}   -> nullopt
 */
TEST(TestProtocolRuntimeKey, CustomTcpInvalidHexFunctionCodeReturnsNullopt)
{
    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, {
        {"function_code", "1001"},
    });

    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, {
        {"function_code", "H"},
    });

    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, {
        {"function_code", "H12G4"},
    });

    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, {
        {"function_code", "h1001"},
    });
}

/**
 * 测试思路：
 * 1. 不支持的协议类型不能生成运行键。
 * 2. 这类错误应在 manager/repository 写 DB 前被识别。
 *
 * 示例：
 *
 *   ProtocolType::kUnknown + {"method":"GET","path":"/api"}
 *        |
 *        v
 *   nullopt
 */
TEST(TestProtocolRuntimeKey, UnknownProtocolTypeReturnsNullopt)
{
    ExpectNoRuntimeKey(ProtocolType::kUnknown, {
        {"method", "GET"},
        {"path", "/api"},
    });
}

/**
 * 测试思路：
 * 1. req_cfg 来自前端 JSON，字段类型可能错误。
 * 2. 类型错误不能让 nlohmann::json 的异常逃逸到上层业务流程。
 * 3. 期望生成器统一返回 nullopt，让调用方按“配置非法”处理。
 *
 * 示例：
 *
 *   {"method":1,"path":"/api"}          -> nullopt
 *   {"method":"GET","path":123}         -> nullopt
 *   {"function_code":1001}              -> nullopt
 */
TEST(TestProtocolRuntimeKey, WrongJsonFieldTypeReturnsNulloptWithoutThrowing)
{
    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", 1},
        {"path", "/api"},
    });

    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", "GET"},
        {"path", 123},
    });

    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, {
        {"function_code", 1001},
    });
}

/**
 * 测试思路：
 * 1. runtime_key 是 DB 唯一索引的输入，生成器不应该要求 req_cfg 顶层一定是 object。
 * 2. 当前调用方可能传入 array/null/string 等错误 JSON；这些输入不能抛异常。
 * 3. 统一返回 nullopt，让 manager/service 按“配置非法”处理。
 *
 * 示例：
 *
 *   []              -> nullopt
 *   null            -> nullopt
 *   "GET /api"      -> nullopt
 */
TEST(TestProtocolRuntimeKey, NonObjectRequestCfgReturnsNulloptWithoutThrowing)
{
    ExpectNoRuntimeKey(ProtocolType::kHttp, nljson::array());
    ExpectNoRuntimeKey(ProtocolType::kHttp, nullptr);
    ExpectNoRuntimeKey(ProtocolType::kCustomTcp, "H1001");
}

/**
 * 测试思路：
 * 1. HTTP runtime_key 必须稳定但不替调用方做隐式规范化。
 * 2. 小写 method、缺少前导斜杠 path 都应失败，避免 DB 和 runtime 看到不同语义。
 * 3. 合法的 DELETE/HEAD 等当前 HttpRequest 支持的方法应正常生成。
 *
 * 示例：
 *
 *   {"method":"get","path":"/api"}      -> nullopt
 *   {"method":"DELETE","path":"/api"}   -> HTTP|DELETE|/api
 */
TEST(TestProtocolRuntimeKey, HttpMethodUsesExistingHttpRequestMethodRules)
{
    ExpectNoRuntimeKey(ProtocolType::kHttp, {
        {"method", "get"},
        {"path", "/api"},
    });

    ExpectRuntimeKey(ProtocolType::kHttp, {
        {"method", "DELETE"},
        {"path", "/api/users/42"},
    }, "HTTP|DELETE|/api/users/42");

    ExpectRuntimeKey(ProtocolType::kHttp, {
        {"method", "HEAD"},
        {"path", "/health"},
    }, "HTTP|HEAD|/health");
}
