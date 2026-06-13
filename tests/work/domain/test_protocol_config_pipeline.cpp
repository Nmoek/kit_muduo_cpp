/**
 * @file test_protocol_config_pipeline.cpp
 * @brief 协议配置校验与运行态构建 pipeline 测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-11
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_pattern_spec.h"
#include "domain/custom_tcp_protocol_item.h"
#include "domain/http_protocol_item.h"
#include "domain/protocol.h"
#include "domain/protocol_config_pipeline.h"
#include "gtest/gtest.h"

#include "test_custom_project_server.h"

#include <memory>
#include <string>
#include <vector>

using namespace kit_domain;
using nljson = nlohmann::json;

namespace {

nljson JsonFromString(const std::string &text)
{
    return nljson::parse(text);
}

nljson HttpReqCfg(const std::string &method = "GET", const std::string &path = "/api/users")
{
    return {
        {"method", method},
        {"path", path},
        {"headers", {{"X-Test", "1"}}},
    };
}

nljson HttpRespCfg(const std::string &status_code = "200")
{
    return {
        {"status_code", status_code},
        {"headers", {{"Content-Type", "application/json"}}},
    };
}

Protocol MakeHttpProtocol()
{
    Protocol protocol;
    protocol.m_id = 1001;
    protocol.m_name = "http-pipeline";
    protocol.m_type = ProtocolType::kHttp;
    protocol.m_projectId = 2001;
    protocol.m_runtimeKey = "HTTP|GET|/api/users";
    protocol.m_status = ProtocolStatus::kValid;
    protocol.m_configState = ProtocolConfigState::kOff;
    protocol.m_reqBodyType = ProtocolBodyType::kJson;
    protocol.m_respBodyType = ProtocolBodyType::kJson;
    protocol.m_reqBodyDataStatus = 1;
    protocol.m_respBodyDataStatus = 1;
    protocol.m_reqCfg = HttpReqCfg();
    protocol.m_respCfg = HttpRespCfg();
    protocol.m_reqBodyData = {'{', '}'};
    protocol.m_respBodyData = {'{', '"', 'o', 'k', '"', ':', 't', 'r', 'u', 'e', '}'};
    protocol.m_isEndian = false;
    return protocol;
}

CustomTcpPatternSpec MakeTcpPatternSpec()
{
    auto spec = CustomTcpPatternSpec::FromJson(JsonFromString(pattern_json_str1));
    EXPECT_TRUE(spec.has_value());
    return spec.value();
}

std::shared_ptr<CustomTcpPattern> MakeTcpPattern()
{
    auto pattern = CustomTcpPatternFactory::Create(JsonFromString(pattern_json_str1));
    EXPECT_NE(pattern, nullptr);
    return pattern;
}

Protocol MakeTcpProtocol()
{
    Protocol protocol;
    protocol.m_id = 3001;
    protocol.m_name = "tcp-pipeline";
    protocol.m_type = ProtocolType::kCustomTcp;
    protocol.m_projectId = 4001;
    protocol.m_runtimeKey = "TCP|H0100";
    protocol.m_status = ProtocolStatus::kValid;
    protocol.m_configState = ProtocolConfigState::kOff;
    protocol.m_reqBodyType = ProtocolBodyType::kBinary;
    protocol.m_respBodyType = ProtocolBodyType::kBinary;
    protocol.m_reqBodyDataStatus = 0;
    protocol.m_respBodyDataStatus = 1;
    protocol.m_reqCfg = JsonFromString(req_cfg1);
    protocol.m_respCfg = JsonFromString(resp_cfg1);
    protocol.m_respBodyData = {'o', 'k'};
    protocol.m_isEndian = true;
    return protocol;
}

bool CanLeaveReConfig(ProtocolType type,
                      const nljson &req_cfg,
                      const nljson &resp_cfg,
                      const std::optional<CustomTcpPatternSpec> &tcp_spec = std::nullopt)
{
    const auto req_result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = type,
        .side = ProtocolSide::kRequest,
        .cfg = req_cfg,
        .custom_tcp_pattern_spec = tcp_spec,
    });
    if(!req_result.ok || !req_result.runtime_key.has_value())
    {
        return false;
    }

    const auto resp_result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = type,
        .side = ProtocolSide::kResponse,
        .cfg = resp_cfg,
        .custom_tcp_pattern_spec = tcp_spec,
    });
    return resp_result.ok && !resp_result.runtime_key.has_value();
}

} // namespace

/**
 * 测试思路：
 * 1. HTTP request side 是 HTTP 协议运行键的唯一来源。
 * 2. 输入完整 method/path/headers 后，pipeline 应复用 HTTP cfg parser 完成校验。
 * 3. 校验成功后返回 HTTP|<METHOD>|<PATH>，供后续 DB 唯一索引和 runtime 分发使用。
 *
 * 示例：
 *
 *   {"method":"GET","path":"/api/users","headers":{"X-Test":"1"}}
 *        |
 *        v
 *   runtime_key = HTTP|GET|/api/users
 */
TEST(TestProtocolConfigPipeline, HttpRequestCheckConfigReturnsRuntimeKey)
{
    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttp,
        .side = ProtocolSide::kRequest,
        .cfg = HttpReqCfg(),
    });

    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.runtime_key.has_value());
    EXPECT_EQ(result.runtime_key.value(), "HTTP|GET|/api/users");
}

/**
 * 测试思路：
 * 1. HTTPS 与 HTTP 一样使用 method/path 作为运行匹配键，但协议前缀必须区分。
 * 2. 输入完整 HTTPS request cfg 后，pipeline 应返回 HTTPS|<METHOD>|<PATH>。
 * 3. 该用例避免 HTTPS 被误按 HTTP 前缀写入 DB 唯一索引。
 *
 * 示例：
 *
 *   HTTPS + {"method":"POST","path":"/secure/login","headers":{}}
 *        |
 *        v
 *   runtime_key = HTTPS|POST|/secure/login
 */
TEST(TestProtocolConfigPipeline, HttpsRequestCheckConfigReturnsHttpsRuntimeKey)
{
    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttps,
        .side = ProtocolSide::kRequest,
        .cfg = HttpReqCfg("POST", "/secure/login"),
    });

    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.runtime_key.has_value());
    EXPECT_EQ(result.runtime_key.value(), "HTTPS|POST|/secure/login");
}

/**
 * 测试思路：
 * 1. HTTP response side 只校验响应配置是否合法，不参与路由去重。
 * 2. 输入完整 status_code/headers 后，pipeline 应返回 ok。
 * 3. response side 成功时 runtime_key 必须保持为空，避免响应配置误占唯一索引。
 *
 * 示例：
 *
 *   {"status_code":"200","headers":{"Content-Type":"application/json"}}
 *        |
 *        v
 *   ok = true, runtime_key = nullopt
 */
TEST(TestProtocolConfigPipeline, HttpResponseCheckConfigDoesNotReturnRuntimeKey)
{
    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttp,
        .side = ProtocolSide::kResponse,
        .cfg = HttpRespCfg(),
    });

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. pipeline 的模板方法先做顶层字段白名单校验，再进入具体 parser。
 * 2. HTTP request 不允许出现 method/path/headers 之外的字段。
 * 3. 未知字段应直接失败，避免前端拼错字段后被静默忽略。
 *
 * 示例：
 *
 *   {"method":"GET","path":"/api","headers":{},"route":"/wrong"}
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolConfigPipeline, HttpUnknownFieldFailsBeforeRuntimeKeyGeneration)
{
    auto cfg = HttpReqCfg("GET", "/api");
    cfg["route"] = "/wrong";

    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttp,
        .side = ProtocolSide::kRequest,
        .cfg = cfg,
    });

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. HTTP request cfg 必须同时具备 method/path/headers，response cfg 必须具备 status_code/headers。
 * 2. 缺少必填字段时，即使 JSON 是 object 且字段都在白名单内，也不能通过具体 parser。
 * 3. 该用例保护 UpdateCfg merge patch 后的完整 cfg 校验，避免半成品配置进入 kOff/kOn。
 *
 * 示例：
 *
 *   request:  {"method":"GET","headers":{}}      -> failed
 *   response: {"status_code":"200"}              -> failed
 */
TEST(TestProtocolConfigPipeline, HttpMissingRequiredFieldFails)
{
    const nljson req_missing_path = {
        {"method", "GET"},
        {"headers", nljson::object()},
    };
    const auto req_result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttp,
        .side = ProtocolSide::kRequest,
        .cfg = req_missing_path,
    });

    EXPECT_FALSE(req_result.ok);
    EXPECT_FALSE(req_result.runtime_key.has_value());

    const nljson resp_missing_headers = {
        {"status_code", "200"},
    };
    const auto resp_result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttp,
        .side = ProtocolSide::kResponse,
        .cfg = resp_missing_headers,
    });

    EXPECT_FALSE(resp_result.ok);
    EXPECT_FALSE(resp_result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. BuildItem 是运行态路径入口，必须同时校验 req_cfg 和 resp_cfg。
 * 2. HTTP req/resp 都合法时，构建结果应带 runtime_key 和非空 HttpProtocolItem。
 * 3. item 的基础字段来自当前落地实现中的 Protocol 快照，避免运行态 item 缺少 id/project/body。
 *
 * 示例：
 *
 *   Protocol(req=GET /api/users, resp=200)
 *        |
 *        v
 *   item != nullptr, runtime_key = HTTP|GET|/api/users
 */
TEST(TestProtocolConfigPipeline, HttpBuildItemValidatesBothSidesAndCreatesItem)
{
    const auto protocol = MakeHttpProtocol();

    const auto result = ProtocolConfigPipeline::buildItem(ProtocolItemBuildSpec{
        .full_config = ProtocolFullConfigSpec{
            .type = ProtocolType::kHttp,
            .req_cfg = protocol.m_reqCfg,
            .resp_cfg = protocol.m_respCfg,
        },
        .ori_protocol = protocol,
    });

    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.runtime_key.has_value());
    EXPECT_EQ(result.runtime_key.value(), "HTTP|GET|/api/users");
    ASSERT_NE(result.item, nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<HttpProtocolItem>(result.item), nullptr);
    EXPECT_EQ(result.item->getId(), protocol.m_id);
    EXPECT_EQ(result.item->getProjectId(), protocol.m_projectId);
}

/**
 * 测试思路：
 * 1. Custom TCP request side 需要当前项目的 pattern spec 才能校验功能码长度和 fields。
 * 2. 输入合法 function_code 和 fields 后，request side 应返回 TCP|<FUNCTION_CODE>。
 * 3. 这个用例保护 fields 白名单和 TCP runtime_key 生成闭环。
 *
 * 示例：
 *
 *   pattern(function_code byte_len=2) + {"function_code":"H0100","fields":{...}}
 *        |
 *        v
 *   runtime_key = TCP|H0100
 */
TEST(TestProtocolConfigPipeline, CustomTcpRequestCheckConfigReturnsRuntimeKey)
{
    const auto spec = MakeTcpPatternSpec();

    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kCustomTcp,
        .side = ProtocolSide::kRequest,
        .cfg = JsonFromString(req_cfg1),
        .custom_tcp_pattern_spec = spec,
    });

    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.runtime_key.has_value());
    EXPECT_EQ(result.runtime_key.value(), "TCP|H0100");
}

/**
 * 测试思路：
 * 1. Custom TCP response side 同样要按 pattern spec 校验 cfg 格式。
 * 2. response side 合法时只代表响应配置可被 runtime 使用，不代表可作为运行键。
 * 3. 成功结果必须不带 runtime_key，避免响应功能码污染协议去重。
 *
 * 示例：
 *
 *   pattern + {"function_code":"H1080","fields":{...}}
 *        |
 *        v
 *   ok = true, runtime_key = nullopt
 */
TEST(TestProtocolConfigPipeline, CustomTcpResponseCheckConfigDoesNotReturnRuntimeKey)
{
    const auto spec = MakeTcpPatternSpec();

    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kCustomTcp,
        .side = ProtocolSide::kResponse,
        .cfg = JsonFromString(resp_cfg1),
        .custom_tcp_pattern_spec = spec,
    });

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. Custom TCP cfg 的 function_code 长度必须与 pattern 中的功能码字段 byte_len 匹配。
 * 2. pattern 缺失时无法做这个强校验，也无法安全生成可运行配置。
 * 3. 因此即使 cfg 字面上包含 function_code/fields，pipeline 也必须失败。
 *
 * 示例：
 *
 *   no pattern spec + {"function_code":"H0100","fields":{}}
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolConfigPipeline, CustomTcpCheckConfigFailsWithoutPatternSpec)
{
    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kCustomTcp,
        .side = ProtocolSide::kRequest,
        .cfg = JsonFromString(req_cfg1),
    });

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. Custom TCP function_code 必须是 H 开头、长度匹配 pattern 功能码字段、内容为十六进制。
 * 2. fields 中按 byte_pos 配置的值同样必须是 H 开头且长度匹配对应字段。
 * 3. 非法 function_code 或字段值都必须失败，避免生成不可序列化的 runtime item。
 *
 * 示例：
 *
 *   {"function_code":"H12G4","fields":{}}             -> failed
 *   {"function_code":"H0100","fields":{"4":"HXYZ"}}   -> failed
 */
TEST(TestProtocolConfigPipeline, CustomTcpInvalidHexFails)
{
    const auto spec = MakeTcpPatternSpec();

    auto invalid_function_code = JsonFromString(req_cfg1);
    invalid_function_code["function_code"] = "H12G4";
    const auto function_code_result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kCustomTcp,
        .side = ProtocolSide::kRequest,
        .cfg = invalid_function_code,
        .custom_tcp_pattern_spec = spec,
    });

    EXPECT_FALSE(function_code_result.ok);
    EXPECT_FALSE(function_code_result.runtime_key.has_value());

    auto invalid_field_value = JsonFromString(req_cfg1);
    invalid_field_value["fields"]["4"] = "HXYZ";
    const auto field_result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kCustomTcp,
        .side = ProtocolSide::kRequest,
        .cfg = invalid_field_value,
        .custom_tcp_pattern_spec = spec,
    });

    EXPECT_FALSE(field_result.ok);
    EXPECT_FALSE(field_result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. Custom TCP BuildItem 是上线、启动 hydrate 等运行态路径的构建入口。
 * 2. 它不仅需要 pattern spec 校验 req/resp，还需要 runtime pattern 对象创建 CustomTcpProtocolItem。
 * 3. 两个依赖都存在时，结果应返回非空 CustomTcpProtocolItem 和 request runtime_key。
 *
 * 示例：
 *
 *   Protocol(TCP H0100/H1080) + pattern spec + pattern runtime object
 *        |
 *        v
 *   item != nullptr, runtime_key = TCP|H0100
 */
TEST(TestProtocolConfigPipeline, CustomTcpBuildItemValidatesBothSidesAndCreatesItem)
{
    const auto protocol = MakeTcpProtocol();
    const auto spec = MakeTcpPatternSpec();
    const auto pattern = MakeTcpPattern();

    const auto result = ProtocolConfigPipeline::buildItem(ProtocolItemBuildSpec{
        .full_config = ProtocolFullConfigSpec{
            .type = ProtocolType::kCustomTcp,
            .req_cfg = protocol.m_reqCfg,
            .resp_cfg = protocol.m_respCfg,
        },
        .ori_protocol = protocol,
        .custom_tcp_pattern = pattern,
    });

    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.runtime_key.has_value());
    EXPECT_EQ(result.runtime_key.value(), "TCP|H0100");
    ASSERT_NE(result.item, nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<CustomTcpProtocolItem>(result.item), nullptr);
    EXPECT_EQ(result.item->getId(), protocol.m_id);
    EXPECT_EQ(result.item->getProjectId(), protocol.m_projectId);
}

/**
 * 测试思路：
 * 1. pattern spec 只能证明 cfg 符合 schema，不能替代运行态 CustomTcpPattern 对象。
 * 2. BuildItem 在创建 CustomTcpProtocolItem 时必须拿到 runtime pattern。
 * 3. 缺少 runtime pattern 时应构建失败，避免后续 AddProtocolItem 拿到不可工作的 item。
 *
 * 示例：
 *
 *   Protocol + pattern spec + custom_tcp_pattern=nullptr
 *        |
 *        v
 *   build failed
 */
TEST(TestProtocolConfigPipeline, CustomTcpBuildItemFailsWithoutRuntimePattern)
{
    const auto protocol = MakeTcpProtocol();
    const auto spec = MakeTcpPatternSpec();

    const auto result = ProtocolConfigPipeline::buildItem(ProtocolItemBuildSpec{
        .full_config = ProtocolFullConfigSpec{
            .type = ProtocolType::kCustomTcp,
            .req_cfg = protocol.m_reqCfg,
            .resp_cfg = protocol.m_respCfg,
        },
        .ori_protocol = protocol,
    });

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.runtime_key.has_value());
    EXPECT_EQ(result.item, nullptr);
}

/**
 * 测试思路：
 * 1. kReConfig 能否转回 kOff 由 manager 编排，但判断依据来自 pipeline 的 req/resp 双侧完整校验。
 * 2. req_cfg 或 resp_cfg 任意一侧失败，都不能转 kOff；两侧都成功时才允许 manager 在同一事务中转 kOff。
 * 3. 这里用测试 helper 模拟 manager 判定，避免在 pipeline 中引入状态修改职责。
 *
 * 示例：
 *
 *   valid req + invalid resp  -> cannot leave kReConfig
 *   valid req + valid resp    -> can leave kReConfig
 */
TEST(TestProtocolConfigPipeline, ReConfigCanLeaveOnlyWhenBothSidesAreValid)
{
    auto invalid_http_resp = HttpRespCfg();
    invalid_http_resp.erase("headers");
    EXPECT_FALSE(CanLeaveReConfig(ProtocolType::kHttp, HttpReqCfg(), invalid_http_resp));
    EXPECT_TRUE(CanLeaveReConfig(ProtocolType::kHttp, HttpReqCfg(), HttpRespCfg()));

    const auto tcp_spec = MakeTcpPatternSpec();
    auto invalid_tcp_req = JsonFromString(req_cfg1);
    invalid_tcp_req["function_code"] = "H";
    EXPECT_FALSE(CanLeaveReConfig(
        ProtocolType::kCustomTcp,
        invalid_tcp_req,
        JsonFromString(resp_cfg1),
        tcp_spec));
    EXPECT_TRUE(CanLeaveReConfig(
        ProtocolType::kCustomTcp,
        JsonFromString(req_cfg1),
        JsonFromString(resp_cfg1),
        tcp_spec));
}

/**
 * 测试思路：
 * 1. pipeline 是 manager 写 DB/runtime 前的统一校验入口，输入 cfg 必须是 object。
 * 2. array/null/string 等错误 JSON 不能进入具体协议 parser。
 * 3. checkConfig 应返回失败且不生成 runtime_key。
 *
 * 示例：
 *
 *   cfg = [] + HTTP request
 *        |
 *        v
 *   check failed, runtime_key = nullopt
 */
TEST(TestProtocolConfigPipeline, NonObjectCfgFailsBeforePolicyParse)
{
    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kHttp,
        .side = ProtocolSide::kRequest,
        .cfg = nljson::array(),
    });

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.runtime_key.has_value());
}

/**
 * 测试思路：
 * 1. 不支持的协议类型没有 side policy 和 build policy。
 * 2. checkConfig/buildItem 都应明确失败，而不是退化成 HTTP/TCP 的任一默认策略。
 * 3. 这个用例防止新增协议类型时被旧策略误处理。
 *
 * 示例：
 *
 *   ProtocolType::kUnknown + HTTP-like cfg
 *        |
 *        v
 *   check failed, build failed
 */
TEST(TestProtocolConfigPipeline, UnknownProtocolTypeFailsCheckAndBuild)
{
    const auto check = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kUnknown,
        .side = ProtocolSide::kRequest,
        .cfg = HttpReqCfg(),
    });
    EXPECT_FALSE(check.ok);
    EXPECT_FALSE(check.runtime_key.has_value());

    const auto protocol = MakeHttpProtocol();
    const auto build = ProtocolConfigPipeline::buildItem(ProtocolItemBuildSpec{
        .full_config = ProtocolFullConfigSpec{
            .type = ProtocolType::kUnknown,
            .req_cfg = protocol.m_reqCfg,
            .resp_cfg = protocol.m_respCfg,
        },
        .ori_protocol = protocol,
    });

    EXPECT_FALSE(build.ok);
    EXPECT_FALSE(build.runtime_key.has_value());
    EXPECT_EQ(build.item, nullptr);
}

/**
 * 测试思路：
 * 1. buildItem 是上线/启动 hydrate 的完整配置入口，request 和 response 任一侧失败都不能创建 item。
 * 2. request 合法但 response 缺少 headers 时，应停在 response policy。
 * 3. 返回失败且 item=nullptr，避免 runtime 拿到半初始化协议项。
 *
 * 示例：
 *
 *   req:  GET /api/users
 *   resp: {"status_code":"200"}
 *        |
 *        v
 *   build failed
 */
TEST(TestProtocolConfigPipeline, HttpBuildItemFailsWhenResponseSideInvalid)
{
    auto protocol = MakeHttpProtocol();
    protocol.m_respCfg.erase("headers");

    const auto result = ProtocolConfigPipeline::buildItem(ProtocolItemBuildSpec{
        .full_config = ProtocolFullConfigSpec{
            .type = ProtocolType::kHttp,
            .req_cfg = protocol.m_reqCfg,
            .resp_cfg = protocol.m_respCfg,
        },
        .ori_protocol = protocol,
    });

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.runtime_key.has_value());
    EXPECT_EQ(result.item, nullptr);
}

/**
 * 测试思路：
 * 1. Custom TCP request/response 顶层字段都只允许 function_code 和 fields。
 * 2. response side 出现未知字段时，同样必须失败，不能只保护 request side。
 * 3. 这样响应配置的前端拼写错误不会被静默保存到可运行配置。
 *
 * 示例：
 *
 *   {"function_code":"H1080","fields":{...},"body":"bad"}
 *        |
 *        v
 *   check failed
 */
TEST(TestProtocolConfigPipeline, CustomTcpResponseUnknownFieldFails)
{
    const auto spec = MakeTcpPatternSpec();
    auto resp_cfg = JsonFromString(resp_cfg1);
    resp_cfg["body"] = "bad";

    const auto result = ProtocolConfigPipeline::checkConfig(ProtocolSideConfigSpec{
        .type = ProtocolType::kCustomTcp,
        .side = ProtocolSide::kResponse,
        .cfg = resp_cfg,
        .custom_tcp_pattern_spec = spec,
    });

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.runtime_key.has_value());
}
