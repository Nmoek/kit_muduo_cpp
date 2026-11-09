#ifndef KIT_TESTS_LOG_TEST_SUPPORT_H_
#define KIT_TESTS_LOG_TEST_SUPPORT_H_

#include "base/compression.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace log_test {

// 独立临时 YAML 初始化真实全局配置；不会读取或重写仓库 config/config.yaml。
// backend 为空时使用应用默认值，专项用例可显式选择 mmap_buffered。
void InitializeAppConfig(std::string_view backend = {});

class TempDirectory
{
public:
    TempDirectory()
    {
        std::string pattern = (std::filesystem::temp_directory_path()
            / "kit_log_41_XXXXXX").string();
        if (!::mkdtemp(pattern.data()))
            throw std::runtime_error("cannot create log test directory");
        path_ = pattern;
    }
    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    std::string file(const std::string& name) const { return (path_ / name).string(); }
private:
    std::filesystem::path path_;
};

inline void WriteFile(const std::string& path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write " + path);
}

inline std::string ReadFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

inline std::string DecompressFile(const std::string& path)
{
    const auto bytes = ReadFile(path);
    auto codec = kit_muduo::CompressionCodec::Create();
    std::string output;
    const auto result = codec->decompress(
        {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()},
        [&output](kit_muduo::Span<const uint8_t> chunk) {
            output.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            return kit_muduo::CompressionCallbackResult::Ok();
        });
    if (!result.ok()) throw std::runtime_error(result.message);
    return output;
}

// 确定性地暂停工作任务；测试必须在 worker 所有者之后声明释放保护器。
class Gate
{
public:
    void enterAndWait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++entered_;
        cv_.notify_all();
        cv_.wait(lock, [this] { return released_; });
    }
    bool waitForEntered(size_t count = 1)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(3),
            [this, count] { return entered_ >= count; });
    }
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t entered_{0};
    bool released_{false};
};

class ReleaseGateOnExit
{
public:
    explicit ReleaseGateOnExit(Gate& gate) : gate_(gate) {}
    ~ReleaseGateOnExit() { gate_.release(); }
private:
    Gate& gate_;
};

} // namespace log_test
#endif
