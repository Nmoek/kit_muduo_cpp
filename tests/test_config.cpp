/**
 * @file test_config.cpp
 * @brief 配置系统和应用配置测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-04
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "app_config.h"
#include "base/config.h"
#include "base/config_codec.h"
#include "base/config_codec_policy.h"
#include "base/config_context.h"
#include "base/lexical_cast.h"
#include "base/log_config.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>

using namespace kit_muduo;
using namespace kit_app;

namespace {

struct PathScope {};
struct ScopeIsolationYaml {};
struct ScopeIsolationJson {};
struct AncestorFirstScope {};
struct DescendantFirstScope {};
struct LogConflictScope {};
struct LoadPolicyScope {};
struct TransactionScope {};
struct FreezeScope {};
struct CodecScope {};
struct FileScope {};
struct LogConfigScope {};
struct AppConfigScope {};
struct ValidationScope {};
struct AppConfigDefaultFileScope {};
struct AppConfigEnvironmentExportScope {};
struct AppConfigExistingFileScope {};
struct AppConfigDirectoryTargetScope {};

using YamlPolicy = ConfigYamlPolicy;
using JsonPolicy = ConfigJsonPolicy;

template<typename Scope>
using ScopedYamlConfig = Config<YamlPolicy, Scope>;

template<typename Scope>
using ScopedJsonConfig = Config<JsonPolicy, Scope>;

class TempDirectory
{
public:
    explicit TempDirectory(const std::string& name)
    {
        static std::atomic_uint64_t sequence{0};
        path_ = std::filesystem::temp_directory_path()
            / ("kit_config_test_" + std::to_string(::getpid()) + "_"
                + name + "_" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class TempConfigFile
{
public:
    TempConfigFile(const std::string& name, const std::string& content)
        :directory_(name)
        ,path_(directory_.path() / "config.yaml")
    {
        std::ofstream output(path_);
        if(!output.is_open())
        {
            throw std::runtime_error("cannot create temporary config file");
        }
        output << content;
        if(!output.good())
        {
            throw std::runtime_error("cannot write temporary config file");
        }
    }

    const std::string stringPath() const
    {
        return path_.string();
    }

private:
    TempDirectory directory_;
    std::filesystem::path path_;
};

template<class ConfigType, class Var, class T>
void SetConfigValue(ConfigType& config, const Var& variable, T value,
    const std::string& path)
{
    config.load([&](auto& batch) {
        batch.prepareValue(variable, std::move(value), ConfigContext{
            .source = "test",
            .node_path = path,
        });
    });
}

template<class ConfigType, class T>
void ExpectValue(ConfigType& config,
    const std::string& path,
    const T& expected)
{
    auto variable = config.template lookUp<T>(path);
    ASSERT_NE(variable, nullptr);
    ASSERT_NE(variable->value(), nullptr);
    EXPECT_EQ(*variable->value(), expected);
}

} // namespace

/*
测试思路：路径是配置注册表、YAML 展开和错误上下文共同使用的基础协议，先验证
合法路径归一化、非法路径拒绝、祖先判断，以及错误路径和 source/location 的保留规则。

示例："System.HTTP.Port" -> "system.http.port"；"http."、"http..port"
和空字符串都必须在注册/规范化阶段失败。
*/
TEST(TestConfig, NormalizesPathsAndBuildsErrorContext)
{
    EXPECT_TRUE(CheckConfigPathSegment("http_2"));
    EXPECT_FALSE(CheckConfigPathSegment(""));
    EXPECT_FALSE(CheckConfigPathSegment("http-port"));
    EXPECT_FALSE(CheckConfigPathSegment("http.port"));

    EXPECT_EQ(NormalizeConfigPathSegment("HTTP_Port"), "http_port");
    EXPECT_EQ(NormalizeFullConfigPath("System.HTTP.Port"), "system.http.port");
    EXPECT_EQ(SplitConfigNodePath("System.HTTP.Port"),
        std::vector<std::string>({"system", "http", "port"}));
    EXPECT_EQ(SpliceConfigNodePath("System.HTTP.Port"), "system.http.port");
    EXPECT_EQ(JoinConfigPath("system.http", "port"), "system.http.port");
    EXPECT_TRUE(IsAncestorConfigPath("system", "system.http"));
    EXPECT_FALSE(IsAncestorConfigPath("system", "systematic"));
    EXPECT_FALSE(IsAncestorConfigPath("system.http", "system"));

    for(const std::string invalid : {"", ".http", "http.", "http..port",
        "http-port"})
    {
        EXPECT_THROW(NormalizeFullConfigPath(invalid), ConfigError)
            << "invalid path: " << invalid;
    }

    ConfigError original(ConfigContext{
        .source = "config.yaml",
        .node_path = "items",
        .line = 4,
        .column = 7,
    }, "invalid value");
    const ConfigError indexed = original.prependPath("[2]");
    EXPECT_EQ(indexed.context().node_path, "[2].items");
    EXPECT_EQ(indexed.reason(), "invalid value");
    EXPECT_NE(std::string(indexed.what()).find("config.yaml"), std::string::npos);

    const ConfigError nested = indexed.prependPath("system");
    EXPECT_EQ(nested.context().node_path, "system[2].items");
    EXPECT_EQ(nested.context().source, "config.yaml");
    EXPECT_EQ(nested.context().line, 4U);
    EXPECT_EQ(nested.context().column, 7U);
    EXPECT_NE(std::string(nested.what()).find("config.yaml:4:7 at system[2].items"),
        std::string::npos);

    const ConfigError with_source = ConfigError(ConfigContext{}, "bad")
        .withSourceIfEmpty("fallback.yaml");
    EXPECT_EQ(with_source.context().source, "fallback.yaml");
    EXPECT_EQ(with_source.context().node_path, "");

    const ConfigError with_fallback = ConfigError(ConfigContext{
        .node_path = "field",
    }, "bad").withFallbackContext(ConfigContext{
        .source = "fallback.yaml",
        .node_path = "fallback.field",
        .line = 8,
        .column = 3,
    });
    EXPECT_EQ(with_fallback.context().source, "fallback.yaml");
    EXPECT_EQ(with_fallback.context().node_path, "field");
    EXPECT_EQ(with_fallback.context().line, 8U);
    EXPECT_EQ(with_fallback.context().column, 3U);
}

/*
测试思路：环境变量是配置系统唯一的 string -> scalar 转换入口，必须拒绝空白、尾部
字符、溢出和无符号负数，同时保留 bool 的大小写无关语义。

示例："TrUe"、"1" -> true；"42x"、" 42"、"-1"(无符号) -> BadLexicalCast。
*/
TEST(TestConfig, LexicalCastUsesStrictScalarConversions)
{
    using StringToString = LexicalCast<std::string, std::string>;
    using StringToBool = LexicalCast<std::string, bool>;
    using StringToInt32 = LexicalCast<std::string, int32_t>;
    using StringToUint16 = LexicalCast<std::string, uint16_t>;
    using Int32ToString = LexicalCast<int32_t, std::string>;
    using Uint16ToString = LexicalCast<uint16_t, std::string>;
    using BoolToString = LexicalCast<bool, std::string>;

    EXPECT_EQ(StringToString{}("value"), "value");
    EXPECT_TRUE(StringToBool{}("TrUe"));
    EXPECT_TRUE(StringToBool{}("1"));
    EXPECT_FALSE(StringToBool{}("FALSE"));
    EXPECT_FALSE(StringToBool{}("0"));
    EXPECT_EQ(StringToInt32{}("-42"), -42);
    EXPECT_EQ(StringToUint16{}("65535"), 65535);
    EXPECT_EQ(Int32ToString{}(-42), "-42");
    EXPECT_EQ(Uint16ToString{}(65535), "65535");
    EXPECT_EQ(BoolToString{}(true), "true");
    EXPECT_EQ(BoolToString{}(false), "false");

    for(const std::string invalid : {"", " true", "true ", "yes", "2"})
    {
        EXPECT_THROW(StringToBool{}(invalid), BadLexicalCast)
            << "invalid bool: " << invalid;
    }
    for(const std::string invalid : {"", "42x", " 42", "-1"})
    {
        EXPECT_THROW(StringToUint16{}(invalid), BadLexicalCast)
            << "invalid uint16: " << invalid;
    }
    for(const std::string invalid : {"", "2147483648", "-2147483649"})
    {
        EXPECT_THROW(StringToInt32{}(invalid), BadLexicalCast)
            << "invalid int32: " << invalid;
    }

    try
    {
        (void)StringToUint16{}("65536");
        FAIL() << "overflow must throw";
    }
    catch(const BadLexicalCast& error)
    {
        EXPECT_EQ(error.fromType(), "string");
        EXPECT_EQ(error.toType(), typeid(uint16_t).name());
        EXPECT_NE(error.reason().find("out-of-range"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("lexical cast"), std::string::npos);
    }
}

/*
测试思路：验证完整模板特化确实隔离注册表、真值和 Freeze 状态，并覆盖同名同类型
复用、同名异类型拒绝，以及祖先/后代冲突的两个注册顺序。

示例：同一 Scope 注册 "HTTP.Port" 两次返回同一个变量；另一 Scope 注册同名 key
仍然拥有独立默认值；先注册 http 再注册 http.port 与反向注册都失败。
*/
TEST(TestConfig, RegistryScopesAndPathConflictsAreEnforced)
{
    auto& yaml = ScopedYamlConfig<ScopeIsolationYaml>::Instance();
    auto yaml_port = yaml.lookAndCreate<uint16_t>("HTTP.Port", 5555, "port");
    auto same_yaml_port = yaml.lookAndCreate<uint16_t>("http.port", 6666);
    ASSERT_EQ(yaml_port, same_yaml_port);
    EXPECT_EQ(yaml_port->name(), "http.port");
    EXPECT_EQ(yaml_port->description(), "port");

    auto& json = ScopedJsonConfig<ScopeIsolationJson>::Instance();
    auto json_port = json.lookAndCreate<uint16_t>("http.port", 7777);
    ASSERT_NE(json_port, nullptr);
    EXPECT_NE(static_cast<const void*>(yaml_port.get()),
        static_cast<const void*>(json_port.get()));
    EXPECT_EQ(*yaml_port->value(), 5555);
    EXPECT_EQ(*json_port->value(), 7777);

    EXPECT_THROW(yaml.lookAndCreate<int32_t>("http.port", 1), ConfigError);

    auto& ancestor_first = ScopedYamlConfig<AncestorFirstScope>::Instance();
    ASSERT_NE(ancestor_first.lookAndCreate<int32_t>("http", 1), nullptr);
    EXPECT_THROW(ancestor_first.lookAndCreate<int32_t>("http.port", 2), ConfigError);

    auto& descendant_first = ScopedYamlConfig<DescendantFirstScope>::Instance();
    ASSERT_NE(descendant_first.lookAndCreate<int32_t>("http.port", 2), nullptr);
    EXPECT_THROW(descendant_first.lookAndCreate<int32_t>("http", 1), ConfigError);

    auto& logs = ScopedYamlConfig<LogConflictScope>::Instance();
    ASSERT_NE(logs.lookAndCreate<LogConfig>("system.logs", DefaultLogConfig()), nullptr);
    EXPECT_THROW(logs.lookAndCreate<std::string>(
        "system.logs.root.formatter", ""), ConfigError);

    for(const std::string invalid : {".http", "http.", "http..port"})
    {
        EXPECT_THROW(yaml.lookAndCreate<int32_t>(invalid, 1), ConfigError)
            << "invalid registration path: " << invalid;
    }
}

/*
测试思路：访问接口和 Node tree 是类型擦除后的观察面，验证 lookUp/lookUpBase 的
类型契约、visit 的空回调保护、完整 key 优先和 Apply -> BuildNodeTree -> Apply 闭环。

示例：注册 system.http.port 后从 YAML 的大写路径加载，再把 buildNodeTree 重新加载，
最终真值和首次加载完全一致。
*/
TEST(TestConfig, VisitLookupAndNodeTreeRoundTrip)
{
    auto& config = ScopedYamlConfig<CodecScope>::Instance();
    auto port = config.lookAndCreate<uint16_t>("system.http.port", 5555);
    auto enabled = config.lookAndCreate<bool>("system.http.enabled", false);
    auto tags = config.lookAndCreate<std::vector<std::string>>("system.http.tags", {});

    EXPECT_EQ(config.lookUp<uint16_t>("SYSTEM.HTTP.PORT"), port);
    EXPECT_EQ(config.lookUp<bool>("system.http.enabled"), enabled);
    EXPECT_EQ(config.lookUp<int32_t>("system.http.missing"), nullptr);
    EXPECT_EQ(config.lookUpBase("system.http.port").get(), port.get());
    EXPECT_THROW(config.lookUp<int32_t>("system.http.port"), ConfigError);
    EXPECT_THROW(config.visit(nullptr), std::invalid_argument);

    YAML::Node input = YAML::Load(R"(
system:
  http:
    port: 8080
    enabled: true
    tags: [api, admin]
)");
    ASSERT_NO_THROW(config.load(input, ConfigContext{.source = "roundtrip.yaml"}));
    EXPECT_EQ(*port->value(), 8080);
    EXPECT_TRUE(*enabled->value());
    EXPECT_EQ(*tags->value(), std::vector<std::string>({"api", "admin"}));

    size_t visited = 0;
    config.visit([&](ConfigVarBase<YamlPolicy>::Ptr variable) {
        ++visited;
        EXPECT_FALSE(variable->name().empty());
        EXPECT_FALSE(variable->toString().empty());
    });
    EXPECT_EQ(visited, 3U);

    const YAML::Node tree = config.buildNodeTree();
    EXPECT_EQ(ScopedYamlConfig<CodecScope>::ToString(tree), R"(system:
  http:
    enabled: true
    port: 8080
    tags:
      - api
      - admin)" );

    ASSERT_NO_THROW(config.load(tree, ConfigContext{.source = "tree.yaml"}));
    EXPECT_EQ(*port->value(), 8080);
    EXPECT_TRUE(*enabled->value());
    EXPECT_EQ(*tags->value(), std::vector<std::string>({"api", "admin"}));
}

/*
测试思路：未知字段按 kReject/kIgnore 分流，前缀必须是 map，根节点必须是 map；所有
失败都应在统一提交前发生，不能把同一份输入中的前一个有效字段半提交。

示例：port=8080 且 unknown=1 时 reject 保留旧 port，ignore 则只应用 port。
*/
TEST(TestConfig, LoadPoliciesAndUnknownKeysAreAtomic)
{
    auto& config = ScopedYamlConfig<LoadPolicyScope>::Instance();
    auto port = config.lookAndCreate<uint16_t>("system.http.port", 5555);
    auto host = config.lookAndCreate<std::string>("system.http.host", "old");

    YAML::Node valid = YAML::Load(R"(
system:
  http:
    port: 8080
    host: new
)");
    ASSERT_NO_THROW(config.load(valid, ConfigContext{.source = "valid.yaml"}));
    EXPECT_EQ(*port->value(), 8080);
    EXPECT_EQ(*host->value(), "new");

    YAML::Node unknown = YAML::Load(R"(
system:
  http:
    port: 9090
    unknown: value
)");
    EXPECT_THROW(config.load(unknown, ConfigContext{.source = "reject.yaml"},
        ConfigLoadPolicy::kReject), ConfigError);
    EXPECT_EQ(*port->value(), 8080);

    ASSERT_NO_THROW(config.load(unknown, ConfigContext{.source = "ignore.yaml"},
        ConfigLoadPolicy::kIgnore));
    EXPECT_EQ(*port->value(), 9090);

    YAML::Node scalar_prefix = YAML::Load("system: 1");
    EXPECT_THROW(config.load(scalar_prefix, ConfigContext{.source = "prefix.yaml"}),
        ConfigError);
    EXPECT_EQ(*port->value(), 9090);

    YAML::Node non_map = YAML::Load("[1, 2]");
    EXPECT_THROW(config.load(non_map, ConfigContext{.source = "root.yaml"}),
        ConfigError);

    YAML::Node duplicate_after_normalization;
    duplicate_after_normalization["SYSTEM"]["HTTP"]["PORT"] = 1;
    duplicate_after_normalization["system"]["http"]["host"] = "duplicate";
    EXPECT_THROW(config.load(duplicate_after_normalization,
        ConfigContext{.source = "duplicate.yaml"}), ConfigError);
    EXPECT_EQ(*port->value(), 9090);
}

/*
测试思路：prepare 阶段只构造候选值，只有全部准备成功才提交；覆盖 Node 解码失败、
回调异常、空目标、跨 Registry 目标、成功提交和 PreparedUpdate 的幂等性。

示例：a=1,b=2 加载 a=10,b=bad 后仍为 1/2；下一次 a=10,b=20 才同时生效。
*/
TEST(TestConfig, BatchCommitIsAtomicAndValidatesTargets)
{
    auto& config = ScopedYamlConfig<TransactionScope>::Instance();
    auto a = config.lookAndCreate<int32_t>("a", 1);
    auto b = config.lookAndCreate<int32_t>("b", 2);

    YAML::Node bad = YAML::Load("a: 10\nb: bad");
    EXPECT_THROW(config.load(bad, ConfigContext{.source = "bad.yaml"}), ConfigError);
    EXPECT_EQ(*a->value(), 1);
    EXPECT_EQ(*b->value(), 2);

    EXPECT_THROW(config.load([&](auto& batch) {
        batch.prepareValue(a, 10, ConfigContext{.node_path = "a"});
        throw std::runtime_error("prepare callback failed");
    }), std::runtime_error);
    EXPECT_EQ(*a->value(), 1);

    auto& other = ScopedYamlConfig<ScopeIsolationYaml>::Instance();
    auto foreign = other.lookAndCreate<int32_t>("foreign", 3);
    EXPECT_THROW(config.load([&](auto& batch) {
        batch.prepareValue(foreign, 4, ConfigContext{.node_path = "foreign"});
    }), ConfigError);
    EXPECT_EQ(*foreign->value(), 3);

    EXPECT_THROW(config.load([&](auto& batch) {
        batch.prepareValue(std::shared_ptr<ConfigVar<int32_t, YamlPolicy>>{}, 4,
            ConfigContext{.node_path = "null"});
    }), ConfigError);

    ASSERT_NO_THROW(config.load([&](auto& batch) {
        batch.prepareValue(a, 10, ConfigContext{.node_path = "a"});
        batch.prepareValue(b, 20, ConfigContext{.node_path = "b"});
    }));
    EXPECT_EQ(*a->value(), 10);
    EXPECT_EQ(*b->value(), 20);

    auto prepared = a->prepareNode(YAML::Node(30), ConfigContext{
        .source = "prepared.yaml",
        .node_path = "a",
    });
    EXPECT_EQ(*a->value(), 10);
    auto moved = std::move(prepared);
    moved.commit();
    moved.commit();
    EXPECT_EQ(*a->value(), 30);
}

/*
测试思路：ConfigVar 的待提交闭包使用 weak_ptr，变量销毁后提交只能安全丢弃；Freeze
是启动写窗口的硬边界，注册、Node load 和批量 load 都必须拒绝。

示例：prepare 了 99 后释放 ConfigVar 再 commit 不崩溃；freeze 后 lookAndCreate/load
均抛 ConfigError。
*/
TEST(TestConfig, PreparedUpdatesAreLifetimeSafeAndFreezeIsReadOnly)
{
    auto temporary = std::make_shared<ConfigVar<int32_t, YamlPolicy>>(
        "temporary", 1);
    auto prepared = temporary->prepareNode(YAML::Node(99), ConfigContext{
        .node_path = "temporary",
    });
    temporary.reset();
    ASSERT_NO_THROW(prepared.commit());

    auto& config = ScopedYamlConfig<FreezeScope>::Instance();
    auto value = config.lookAndCreate<int32_t>("value", 1);
    config.freeze();
    EXPECT_TRUE(config.isFrozen());
    EXPECT_EQ(*value->value(), 1);
    EXPECT_THROW(config.lookAndCreate<int32_t>("other", 2), ConfigError);
    EXPECT_THROW(config.load(YAML::Load("value: 2"), ConfigContext{
        .source = "frozen.yaml",
    }), ConfigError);
    EXPECT_THROW(config.load([&](auto& batch) {
        batch.prepareValue(value, 2, ConfigContext{.node_path = "value"});
    }), ConfigError);
    EXPECT_EQ(*value->value(), 1);
}

/*
测试思路：覆盖 V1 支持的四类容器及其嵌套，确认 YAML/JSON 都通过 Policy 访问节点，
并验证错误路径从内层 sequence/map 逐层累积，重复 map key 不静默覆盖。

示例：{"a\"b": [1, bad]} 的错误路径必须包含 ["a\\\"b"][1]，而不是丢失
容器层级；vector、list、map、unordered_map 回写后保持元素值。
*/
TEST(TestConfig, ContainerCodecsRoundTripAndReportNestedPaths)
{
    const std::vector<int32_t> vector_value{1, 2, 3};
    using YamlVectorIntCodec = ConfigCodec<std::vector<int32_t>, YamlPolicy>;
    using JsonListStringCodec = ConfigCodec<std::list<std::string>, JsonPolicy>;
    using JsonMapVectorIntCodec = ConfigCodec<
        std::map<std::string, std::vector<int32_t>>, JsonPolicy>;
    using YamlUnorderedMapIntCodec = ConfigCodec<
        std::unordered_map<std::string, int32_t>, YamlPolicy>;
    using YamlMapIntCodec = ConfigCodec<std::map<std::string, int32_t>, YamlPolicy>;

    const auto yaml_vector = YamlVectorIntCodec::Encode(
        vector_value);
    EXPECT_EQ(YamlVectorIntCodec::Decode(yaml_vector),
        vector_value);

    const std::list<std::string> list_value{"one", "two"};
    const auto json_list = JsonListStringCodec::Encode(
        list_value);
    EXPECT_EQ(JsonListStringCodec::Decode(json_list),
        list_value);

    const std::map<std::string, std::vector<int32_t>> map_value{
        {"first", {1, 2}}, {"second", {3}},
    };
    const auto json_map = JsonMapVectorIntCodec::Encode(map_value);
    EXPECT_EQ(JsonMapVectorIntCodec::Decode(json_map), map_value);

    const std::unordered_map<std::string, int32_t> unordered_value{
        {"alpha", 1}, {"beta", 2},
    };
    const auto yaml_unordered = YamlUnorderedMapIntCodec::Encode(unordered_value);
    EXPECT_EQ(YamlUnorderedMapIntCodec::Decode(yaml_unordered), unordered_value);

    EXPECT_THROW(YamlVectorIntCodec::Decode(
        YAML::Load("not-a-sequence")), ConfigError);
    EXPECT_THROW(YamlMapIntCodec::Decode(
        YAML::Load("[1, 2]")), ConfigError);

    try
    {
        (void)ConfigCodec<std::map<std::string, std::vector<int32_t>>,
            YamlPolicy>::Decode(YAML::Load("\"a\\\"b\": [1, bad]"));
        FAIL() << "nested decode must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().node_path, "[\"a\\\"b\"][1]");
        EXPECT_NE(std::string(error.what()).find("[\"a\\\"b\"][1]"),
            std::string::npos);
    }

    try
    {
        (void)YamlMapIntCodec::Decode(
            YAML::Load("a: 1\na: 2"));
        FAIL() << "duplicate map key must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().node_path, "[\"a\"]");
        EXPECT_NE(error.reason().find("map key"), std::string::npos);
    }
}

/*
测试思路：Policy 自身负责格式解析和 scalar 节点转换，错误必须转成 ConfigError 并
携带 YAML 行列或 JSON 解析位置；文件入口还要补足 source，避免启动错误无法定位文件。

示例：YAML 的 "port: [" 和 JSON 的缺失 value 都应抛 ConfigError；文件中 port: bad
应报告 source=文件路径、node_path=port。
*/
TEST(TestConfig, PolicyAndFileErrorsCarrySourceAndLocation)
{
    try
    {
        (void)ConfigYamlPolicy::Parse("system:\n  port: [\n");
        FAIL() << "invalid YAML must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().source, "");
        EXPECT_GT(error.context().line, 0U);
        EXPECT_GT(error.context().column, 0U);
        EXPECT_NE(error.reason().find("YAML parse failed"), std::string::npos);
    }

    try
    {
        (void)ConfigJsonPolicy::Parse("{\n  \"port\": }\n");
        FAIL() << "invalid JSON must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_GT(error.context().line, 0U);
        EXPECT_GT(error.context().column, 0U);
        EXPECT_NE(error.reason().find("JSON parse failed"), std::string::npos);
    }

    const std::string missing = "/tmp/kit_config_test_missing_config.yaml";
    EXPECT_THROW(ReadConfigFile(missing), ConfigError);

    TempConfigFile file("file_errors", "port: bad\n");
    auto& config = ScopedYamlConfig<FileScope>::Instance();
    auto port = config.lookAndCreate<int32_t>("port", 1);
    try
    {
        config.load(file.stringPath(), ConfigLoadPolicy::kReject);
        FAIL() << "invalid config file must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().source, file.stringPath());
        EXPECT_EQ(error.context().node_path, "port");
        EXPECT_NE(std::string(error.what()).find(file.stringPath()),
            std::string::npos);
    }
    EXPECT_EQ(*port->value(), 1);
}

/*
测试思路：LogConfig 是 system.logs 的完整结构化值，测试默认配置、logger/appender
字段 Codec、未知/缺失字段和 ValidateLogConfig 的业务约束，确保日志配置错误也能在
配置阶段被捕获。

示例：root + stdout 可以成功 round-trip；file appender 缺 file_path、重复 logger
或无 root 必须拒绝。
*/
TEST(TestConfig, LogConfigCodecAndValidationCoverNestedSchema)
{
    const LogConfig defaults = DefaultLogConfig();
    ASSERT_EQ(defaults.loggers.size(), 5U);
    EXPECT_EQ(defaults.loggers.front().name, "root");
    ASSERT_NO_THROW(ValidateLogConfig(defaults));

    using YamlLogConfigCodec = ConfigCodec<LogConfig, YamlPolicy>;
    const YAML::Node encoded = YamlLogConfigCodec::Encode(defaults);
    const LogConfig decoded = YamlLogConfigCodec::Decode(encoded);
    ASSERT_EQ(decoded.loggers.size(), defaults.loggers.size());
    EXPECT_EQ(decoded.loggers.front().name, "root");
    ASSERT_EQ(decoded.loggers.front().appenders.size(), 1U);
    EXPECT_EQ(decoded.loggers.front().appenders.front().type,
        LogAppenderType::kStdout);

    const YAML::Node custom = YAML::Load(R"(
file:
  flush_threshold: 1024
loggers:
  - name: root
    level: INFO
    formatter: "%m"
    appenders:
      - type: stdout
        level: DEBUG
        formatter: "%m"
)");
    const LogConfig custom_config = YamlLogConfigCodec::Decode(custom);
    ASSERT_EQ(custom_config.loggers.size(), 1U);
    EXPECT_EQ(custom_config.loggers.front().level, LogLevel::INFO);
    EXPECT_EQ(custom_config.file.flush_threshold, 1024U);
    ASSERT_NO_THROW(ValidateLogConfig(custom_config));

    EXPECT_THROW(YamlLogConfigCodec::Decode(YAML::Load(R"(
- name: root
  unknown: value
)")), ConfigError);
    EXPECT_THROW(YamlLogConfigCodec::Decode(YAML::Load(R"(
- name: root
  appenders:
    - level: INFO
)")), ConfigError);
    EXPECT_THROW(YamlLogConfigCodec::Decode(YAML::Load(R"(
- name: root
  appenders:
    - type: invalid
)")), ConfigError);
    EXPECT_THROW(YamlLogConfigCodec::Decode(YAML::Load(R"(
- name: root
  name: duplicate
)")), ConfigError);

    LogConfig no_root;
    no_root.loggers = {LoggerConfig{
        .name = "worker",
    }};
    EXPECT_THROW(ValidateLogConfig(no_root), ConfigError);

    LogConfig duplicate;
    duplicate.loggers = {LoggerConfig{.name = "root"}, LoggerConfig{.name = "root"}};
    EXPECT_THROW(ValidateLogConfig(duplicate), ConfigError);

    LogConfig bad_formatter;
    bad_formatter.loggers = {LoggerConfig{
        .name = "root",
        .formatter = "%unknown",
    }};
    EXPECT_THROW(ValidateLogConfig(bad_formatter), ConfigError);

    LogConfig bad_level;
    bad_level.loggers = {LoggerConfig{
        .name = "root",
        .level = LogLevel::UNKNOW,
    }};
    EXPECT_THROW(ValidateLogConfig(bad_level), ConfigError);

    LogConfig bad_file;
    bad_file.loggers = {LoggerConfig{
        .name = "root",
        .appenders = {LogAppenderConfig{
            .type = LogAppenderType::kFile,
        }},
    }};
    EXPECT_THROW(ValidateLogConfig(bad_file), ConfigError);

    LogConfig bad_stdout;
    bad_stdout.loggers = {LoggerConfig{
        .name = "root",
        .appenders = {LogAppenderConfig{
            .type = LogAppenderType::kStdout,
            .file_path = "not-allowed.log",
        }},
    }};
    EXPECT_THROW(ValidateLogConfig(bad_stdout), ConfigError);

    LogConfig bad_appender_level;
    bad_appender_level.loggers = {LoggerConfig{
        .name = "root",
        .appenders = {LogAppenderConfig{
            .type = LogAppenderType::kStdout,
            .level = LogLevel::UNKNOW,
        }},
    }};
    EXPECT_THROW(ValidateLogConfig(bad_appender_level), ConfigError);

    LogConfig valid_file;
    valid_file.loggers = {LoggerConfig{
        .name = "root",
        .appenders = {LogAppenderConfig{
            .type = LogAppenderType::kFile,
            .file_path = "/tmp/config-test.log",
        }},
    }};
    ASSERT_NO_THROW(ValidateLogConfig(valid_file));
}

/*
测试思路：AppConfigVars 必须注册完整 system/work schema，并按默认值 -> YAML -> 环境
变量的优先级初始化；初始化成功后冻结，句柄只能读取最终值。

示例：YAML 将 port 设为 5000，KIT_HTTP_PORT=6000 后最终值必须是 6000；未被环境
覆盖的 work.interaction 字段保持 YAML 值。
*/
TEST(TestConfig, AppConfigRegistersSchemaAndAppliesPrecedence)
{
    TempDirectory static_root("app_precedence_root");
    TempConfigFile file("app_precedence", std::string(R"(
system:
  http:
    host: 127.0.0.1
    port: 5000
    static_root_path: )") + static_root.path().string() + R"(
    io_threads: 2
  business:
    max_threads: 8
    max_task_queue: 16
    thread_idle_seconds: 30
    submit_timeout_ms: 40
  sqlite_db:
    path: yaml.sqlite
    pool_capacity: 3
    busy_timeout_ms: 100
    synchronous: 2
    sync_schema: false
work:
  runtime:
    loop_capacity: 9
  interaction:
    queue_capacity: 10
    stop_drain_timeout_ms: 11
    capture_max_text_bytes: 12
    capture_max_hex_bytes: 13
    capture_max_binary_attachment_bytes: 14
)");

    auto& config = ScopedYamlConfig<AppConfigScope>::Instance();
    const auto vars = RegisterAppConfigVars(config);
    size_t variable_count = 0;
    config.visit([&](ConfigVarBase<YamlPolicy>::Ptr) { ++variable_count; });
    EXPECT_EQ(variable_count, 20U);

    AppConfigLoadInput input;
    input.yaml_file = file.stringPath();
    input.environment = {
        {"KIT_HTTP_HOST", "env-host"},
        {"KIT_HTTP_PORT", "6000"},
        {"KIT_DB_PATH", "env.sqlite"},
        {"KIT_DB_POOL_CAPACITY", "7"},
        {"KIT_DB_BUSY_TIMEOUT_MS", "200"},
    };
    ASSERT_NO_THROW(InitAppConfig(config, vars, input));
    EXPECT_TRUE(config.isFrozen());
    EXPECT_EQ(*vars.system.http.host->value(), "env-host");
    EXPECT_EQ(*vars.system.http.port->value(), 6000);
    EXPECT_EQ(*vars.system.http.static_root_path->value(), static_root.path().string());
    EXPECT_EQ(*vars.system.http.io_threads->value(), 2);
    EXPECT_EQ(*vars.system.business.max_threads->value(), 8);
    EXPECT_EQ(*vars.system.business.max_task_queue->value(), 16);
    EXPECT_EQ(*vars.system.business.thread_idle_seconds->value(), 30);
    EXPECT_EQ(*vars.system.business.submit_timeout_ms->value(), 40);
    EXPECT_EQ(*vars.system.sqlite_db.path->value(), "env.sqlite");
    EXPECT_EQ(*vars.system.sqlite_db.pool_capacity->value(), 7U);
    EXPECT_EQ(*vars.system.sqlite_db.busy_timeout_ms->value(), 200);
    EXPECT_EQ(*vars.system.sqlite_db.synchronous->value(), 2);
    EXPECT_FALSE(*vars.system.sqlite_db.sync_schema->value());
    EXPECT_EQ(*vars.work.runtime.loop_capacity->value(), 9U);
    EXPECT_EQ(*vars.work.interaction.queue_capacity->value(), 10U);
    EXPECT_EQ(*vars.work.interaction.stop_drain_timeout_ms->value(), 11);
    EXPECT_EQ(*vars.work.interaction.capture_max_text_bytes->value(), 12U);
    EXPECT_EQ(*vars.work.interaction.capture_max_hex_bytes->value(), 13U);
    EXPECT_EQ(*vars.work.interaction.capture_max_binary_attachment_bytes->value(), 14U);
    EXPECT_EQ(vars.system.http.host->name(), "system.http.host");
    EXPECT_EQ(vars.system.sqlite_db.path->name(), "system.sqlite_db.path");
}

/*
测试思路：当指定 YAML 文件不存在时，应用配置应把已经注册的默认树写到目标文件，
并继续完成校验和冻结；目标的父目录不存在时也应一并创建，避免首次部署需要手工建目录。

示例：KIT_CONFIG_FILE=/tmp/config/default/app.yaml 且文件缺失时，输出文件包含
system.http.port=5555、system.logs 和 work.interaction 的默认节点。
*/
TEST(TestConfig, AppConfigWritesDefaultTreeWhenYamlFileIsMissing)
{
    TempDirectory output_directory("app_default_export");
    TempDirectory static_root("app_default_export_root");
    const auto output_path = output_directory.path() / "nested" / "app.yaml";

    auto& config = ScopedYamlConfig<AppConfigDefaultFileScope>::Instance();
    const auto vars = RegisterAppConfigVars(config);
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");

    AppConfigLoadInput input;
    input.yaml_file = output_path.string();
    ASSERT_NO_THROW(InitAppConfig(config, vars, input));

    ASSERT_TRUE(std::filesystem::is_regular_file(output_path));
    EXPECT_TRUE(config.isFrozen());
    const std::string yaml_text = ReadConfigFile(output_path.string());
    const auto root = YamlPolicy::Parse(yaml_text);
    ASSERT_TRUE(YamlPolicy::IsMap(root));
    EXPECT_EQ(YamlPolicy::Decode<std::string>(root["system"]["http"]["host"]),
        "0.0.0.0");
    EXPECT_EQ(YamlPolicy::Decode<uint16_t>(root["system"]["http"]["port"]), 5555);
    EXPECT_EQ(YamlPolicy::Decode<std::string>(
        root["system"]["http"]["static_root_path"]), static_root.path().string());
    EXPECT_TRUE(YamlPolicy::IsSequence(root["system"]["log"]["loggers"]));
    EXPECT_GT(YamlPolicy::Size(root["system"]["log"]["loggers"]), 0U);
    EXPECT_EQ(YamlPolicy::Decode<size_t>(root["work"]["interaction"]["queue_capacity"]),
        4096U);
    EXPECT_EQ(yaml_text,
        ScopedYamlConfig<AppConfigDefaultFileScope>::ToString(config.buildNodeTree()));
}

/*
测试思路：默认文件是给用户编辑的基线，不能混入本次进程的临时环境变量；环境变量
仍需在文件导出后覆盖运行时值，保持既定的默认值 -> 文件 -> 环境变量优先级。

示例：KIT_HTTP_PORT=6000 时进程端口为 6000，但新生成 app.yaml 仍记录默认端口 5555。
*/
TEST(TestConfig, AppConfigDefaultExportExcludesEnvironmentOverrides)
{
    TempDirectory output_directory("app_environment_export");
    TempDirectory static_root("app_environment_export_root");
    const auto output_path = output_directory.path() / "app.yaml";

    auto& config = ScopedYamlConfig<AppConfigEnvironmentExportScope>::Instance();
    const auto vars = RegisterAppConfigVars(config);
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");

    AppConfigLoadInput input;
    input.yaml_file = output_path.string();
    input.environment = {{"KIT_HTTP_PORT", "6000"}};
    ASSERT_NO_THROW(InitAppConfig(config, vars, input));

    EXPECT_EQ(*vars.system.http.port->value(), 6000);
    const auto root = YamlPolicy::Parse(ReadConfigFile(output_path.string()));
    EXPECT_EQ(YamlPolicy::Decode<uint16_t>(root["system"]["http"]["port"]), 5555);
}

/*
测试思路：已有配置文件仍是显式输入，默认导出逻辑只能在文件缺失时触发，绝不能覆盖
用户已经保存的内容；同时保留已有文件加载语义。

示例：已有 YAML 写入 system.http.port=5100 后初始化，运行时端口为 5100，文件内容
逐字保持不变。
*/
TEST(TestConfig, AppConfigKeepsExistingYamlFileAndLoadsIt)
{
    const std::string yaml_text = "system:\n  http:\n    port: 5100\n";
    TempConfigFile file("app_existing_file", yaml_text);
    TempDirectory static_root("app_existing_file_root");

    auto& config = ScopedYamlConfig<AppConfigExistingFileScope>::Instance();
    const auto vars = RegisterAppConfigVars(config);
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");

    AppConfigLoadInput input;
    input.yaml_file = file.stringPath();
    ASSERT_NO_THROW(InitAppConfig(config, vars, input));

    EXPECT_EQ(*vars.system.http.port->value(), 5100);
    EXPECT_EQ(ReadConfigFile(file.stringPath()), yaml_text);
}

/*
测试思路：路径已存在但不是普通文件时，不能把目录或其他对象当作 YAML 读取或覆盖；
报错应关联 KIT_CONFIG_FILE 指定的精确路径，且初始化不得冻结注册表。

示例：KIT_CONFIG_FILE 指向 /tmp/config-dir 时，抛出包含 regular file 原因的 ConfigError。
*/
TEST(TestConfig, AppConfigRejectsDirectoryAsYamlFileTarget)
{
    TempDirectory directory("app_directory_target");
    TempDirectory static_root("app_directory_target_root");

    auto& config = ScopedYamlConfig<AppConfigDirectoryTargetScope>::Instance();
    const auto vars = RegisterAppConfigVars(config);
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");

    AppConfigLoadInput input;
    input.yaml_file = directory.path().string();
    try
    {
        InitAppConfig(config, vars, input);
        FAIL() << "directory config target must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().source, directory.path().string());
        EXPECT_NE(error.reason().find("regular file"), std::string::npos);
    }
    EXPECT_FALSE(config.isFrozen());
}

/*
测试思路：环境变量解析失败发生在 batch prepare 阶段，必须保留 YAML 已有值；同时
覆盖空字符串和所有既定覆盖字段的 context(source=env:NAME, node_path=完整路径)。

示例：YAML host= yaml-host，环境 host=env-host 但 port="bad" 时，host 仍为 yaml-host，
错误定位为 env:KIT_HTTP_PORT/system.http.port。
*/
TEST(TestConfig, AppConfigEnvironmentErrorsDoNotPartiallyCommit)
{
    TempDirectory static_root("app_env_error_root");
    auto& config = ScopedYamlConfig<ValidationScope>::Instance();
    auto vars = RegisterAppConfigVars(config);

    AppConfigLoadInput yaml_input;
    yaml_input.environment = {
        {"KIT_HTTP_HOST", "yaml-host"},
        {"KIT_HTTP_PORT", "5000"},
        {"KIT_DB_PATH", "yaml.sqlite"},
        {"KIT_DB_POOL_CAPACITY", "2"},
        {"KIT_DB_BUSY_TIMEOUT_MS", "10"},
    };
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");
    ASSERT_NO_THROW(config.load([&](auto& batch) {
        PrepareEnvironmentOverrides(batch, vars, yaml_input.environment);
    }));
    EXPECT_EQ(*vars.system.http.host->value(), "yaml-host");
    EXPECT_EQ(*vars.system.http.port->value(), 5000);

    AppConfigLoadInput invalid;
    invalid.environment = {
        {"KIT_HTTP_HOST", "env-host"},
        {"KIT_HTTP_PORT", "bad-port"},
    };
    try
    {
        config.load([&](auto& batch) {
            PrepareEnvironmentOverrides(batch, vars, invalid.environment);
        });
        FAIL() << "invalid environment must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().source, "env:KIT_HTTP_PORT");
        EXPECT_EQ(error.context().node_path, "system.http.port");
        EXPECT_NE(error.reason().find("invalid environment variable value"),
            std::string::npos);
    }
    EXPECT_EQ(*vars.system.http.host->value(), "yaml-host");
    EXPECT_EQ(*vars.system.http.port->value(), 5000);

    AppConfigLoadInput empty;
    empty.environment = {{"KIT_HTTP_HOST", ""}};
    try
    {
        config.load([&](auto& batch) {
            PrepareEnvironmentOverrides(batch, vars, empty.environment);
        });
        FAIL() << "empty environment must fail";
    }
    catch(const ConfigError& error)
    {
        EXPECT_EQ(error.context().source, "env:KIT_HTTP_HOST");
        EXPECT_EQ(error.context().node_path, "system.http.host");
        EXPECT_EQ(error.reason(), "environment variable must not be empty");
    }
}

/*
测试思路：ValidateAppConfigVars 是运行时资源创建前的最后一道校验，逐项制造边界
非法值，确认每个分支报出完整 effective path；最后恢复合法值并验证静态目录和日志
配置的正向路径。

示例：port=0、synchronous=3、loop_capacity=0、capture limit=0 都应失败，恢复后
ValidateAppConfigVars 必须通过。
*/
TEST(TestConfig, AppConfigValidationCoversAllRuntimeBounds)
{
    TempDirectory static_root("app_validation_root");
    auto& config = ScopedYamlConfig<ValidationScope>::Instance();
    auto vars = RegisterAppConfigVars(config);
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");

    ASSERT_NO_THROW(ValidateAppConfigVars(vars));

    auto expect_invalid = [&](const std::string& path, auto variable, auto value) {
        SetConfigValue(config, variable, value, path);
        try
        {
            ValidateAppConfigVars(vars);
            ADD_FAILURE() << "expected invalid value at " << path;
        }
        catch(const ConfigError& error)
        {
            EXPECT_EQ(error.context().source, "effective");
            EXPECT_EQ(error.context().node_path, path);
        }
    };

    expect_invalid("system.http.host", vars.system.http.host, std::string{});
    SetConfigValue(config, vars.system.http.host, std::string("127.0.0.1"),
        "system.http.host");
    expect_invalid("system.http.port", vars.system.http.port, uint16_t{0});
    SetConfigValue(config, vars.system.http.port, uint16_t{5555}, "system.http.port");
    expect_invalid("system.http.static_root_path", vars.system.http.static_root_path,
        std::string{});
    SetConfigValue(config, vars.system.http.static_root_path,
        static_root.path().string(), "system.http.static_root_path");
    expect_invalid("system.http.io_threads", vars.system.http.io_threads, int32_t{0});
    SetConfigValue(config, vars.system.http.io_threads, int32_t{4}, "system.http.io_threads");
    expect_invalid("system.business.max_threads", vars.system.business.max_threads, int32_t{-1});
    SetConfigValue(config, vars.system.business.max_threads, int32_t{4}, "system.business.max_threads");
    expect_invalid("system.business.max_task_queue", vars.system.business.max_task_queue, int32_t{-1});
    SetConfigValue(config, vars.system.business.max_task_queue, int32_t{4}, "system.business.max_task_queue");
    expect_invalid("system.business.thread_idle_seconds",
        vars.system.business.thread_idle_seconds, int32_t{0});
    SetConfigValue(config, vars.system.business.thread_idle_seconds, int32_t{1},
        "system.business.thread_idle_seconds");
    expect_invalid("system.business.submit_timeout_ms",
        vars.system.business.submit_timeout_ms, int32_t{-1});
    SetConfigValue(config, vars.system.business.submit_timeout_ms, int32_t{300},
        "system.business.submit_timeout_ms");
    expect_invalid("system.sqlite_db.path", vars.system.sqlite_db.path, std::string{});
    SetConfigValue(config, vars.system.sqlite_db.path, std::string("kit.sqlite"),
        "system.sqlite_db.path");
    expect_invalid("system.sqlite_db.pool_capacity", vars.system.sqlite_db.pool_capacity,
        size_t{0});
    SetConfigValue(config, vars.system.sqlite_db.pool_capacity, size_t{1},
        "system.sqlite_db.pool_capacity");
    expect_invalid("system.sqlite_db.busy_timeout_ms",
        vars.system.sqlite_db.busy_timeout_ms, int32_t{-1});
    SetConfigValue(config, vars.system.sqlite_db.busy_timeout_ms, int32_t{3000},
        "system.sqlite_db.busy_timeout_ms");
    expect_invalid("system.sqlite_db.synchronous", vars.system.sqlite_db.synchronous,
        int32_t{3});
    SetConfigValue(config, vars.system.sqlite_db.synchronous, int32_t{1},
        "system.sqlite_db.synchronous");
    expect_invalid("work.runtime.loop_capacity", vars.work.runtime.loop_capacity,
        size_t{0});
    SetConfigValue(config, vars.work.runtime.loop_capacity, size_t{1},
        "work.runtime.loop_capacity");
    expect_invalid("work.interaction.queue_capacity", vars.work.interaction.queue_capacity,
        size_t{0});
    SetConfigValue(config, vars.work.interaction.queue_capacity, size_t{1},
        "work.interaction.queue_capacity");
    expect_invalid("work.interaction.stop_drain_timeout_ms",
        vars.work.interaction.stop_drain_timeout_ms, int64_t{-1});
    SetConfigValue(config, vars.work.interaction.stop_drain_timeout_ms, int64_t{0},
        "work.interaction.stop_drain_timeout_ms");
    expect_invalid("work.interaction.capture_max_text_bytes",
        vars.work.interaction.capture_max_text_bytes, size_t{0});
    SetConfigValue(config, vars.work.interaction.capture_max_text_bytes, size_t{1},
        "work.interaction.capture_max_text_bytes");
    expect_invalid("work.interaction.capture_max_hex_bytes",
        vars.work.interaction.capture_max_hex_bytes, size_t{0});
    SetConfigValue(config, vars.work.interaction.capture_max_hex_bytes, size_t{1},
        "work.interaction.capture_max_hex_bytes");
    expect_invalid("work.interaction.capture_max_binary_attachment_bytes",
        vars.work.interaction.capture_max_binary_attachment_bytes, size_t{0});
    SetConfigValue(config, vars.work.interaction.capture_max_binary_attachment_bytes,
        size_t{1}, "work.interaction.capture_max_binary_attachment_bytes");

    ASSERT_NO_THROW(ValidateAppConfigVars(vars));
}

/*
测试思路：全局读取接口必须把“未初始化”和“已 Freeze”区分开；生产 Scope 在本测试
中不执行初始化，避免测试状态污染正式单例，同时验证 GetAppConfigVars 的启动门禁。

示例：在 InitGlobalAppConfig 未被调用时访问 GetAppConfigVars 应抛 logic_error。
*/
TEST(TestConfig, GlobalAppConfigReadRequiresInitialization)
{
    EXPECT_THROW(GetAppConfigVars(), std::logic_error);
}
