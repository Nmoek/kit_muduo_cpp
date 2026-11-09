#include "app_config.h"
#include "log_test_support.h"

#include <optional>

namespace log_test {
namespace {
class ScopedEnvironment
{
public:
    ScopedEnvironment(const char* name, const std::string& value) : name_(name)
    {
        if (const char* old = std::getenv(name)) old_ = old;
        if (::setenv(name, value.c_str(), 1) != 0)
            throw std::runtime_error("cannot set test configuration environment");
    }
    ~ScopedEnvironment()
    {
        if (old_) ::setenv(name_.c_str(), old_->c_str(), 1);
        else ::unsetenv(name_.c_str());
    }
private:
    std::string name_;
    std::optional<std::string> old_;
};
} // namespace

void InitializeAppConfig(std::string_view backend)
{
    TempDirectory directory;
    const auto config_path = directory.file("test.yaml");
    auto yaml = YAML::Load("system:\n  http:\n    static_root_path: " + directory.file(".") + "\n");
    if (!backend.empty())
    {
        // system.log 是整体配置对象；完整编码默认配置，保留必需的 root logger。
        auto config = kit_muduo::DefaultLogConfig();
        config.file.backend = kit_muduo::ConfigCodec<kit_muduo::LogFileBackendType,
            kit_muduo::ConfigYamlPolicy>::Decode(YAML::Node(std::string(backend)));
        yaml["system"]["log"] = kit_muduo::ConfigCodec<kit_muduo::LogConfig,
            kit_muduo::ConfigYamlPolicy>::Encode(config);
    }
    WriteFile(config_path, YAML::Dump(yaml));
    ScopedEnvironment config_file("KIT_CONFIG_FILE", config_path);
    ScopedEnvironment static_root("KIT_HTTP_STATIC_ROOT_PATH", directory.file("."));
    kit_app::InitGlobalAppConfig();
}
} // namespace log_test
