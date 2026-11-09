#include "base/log.h"
#include "base/defer.h"
#include "base/mmap_log_file_backend.h"
#include "base/regular_log_file_backend.h"
#include "base/time_stamp.h"
#include "log_test_support.h"

#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>

using namespace kit_muduo;
using namespace log_test;
using namespace std::chrono_literals;

namespace kit_muduo {
// 使用 LogManager 已声明的 friend 接缝，仅测试目标定义；不改生产可见接口。
class LogSubmissionGuard
{
public:
    explicit LogSubmissionGuard(LogManager& manager)
        : manager_(manager), entered_(manager.tryEnterLogSubmission()) {}
    ~LogSubmissionGuard() { leave(); }
    bool entered() const { return entered_; }
    void leave()
    {
        if (entered_)
        {
            manager_.leaveLogSubmission();
            entered_ = false;
        }
    }
private:
    LogManager& manager_;
    bool entered_;
};
} // namespace kit_muduo

namespace {

LogConfig StdoutConfig()
{
    LogConfig config;
    LoggerConfig root;
    root.name = "root";
    root.level = LogLevel::DEBUG;
    root.formatter = "%m%n";
    root.appenders.push_back({LogAppenderType::kStdout, LogLevel::DEBUG, "", ""});
    config.loggers.push_back(root);
    return config;
}

LogAttr::Ptr Attr(const Logger::Ptr& logger, const std::string& message)
{
    auto attr = std::make_shared<LogAttr>(logger, LogLevel::INFO,
        logger->getName(), "test", __FILE__, __LINE__, 0,
        ::pthread_self(), ::getpid(), "test", 0);
    attr->getSS() << message;
    attr->seal();
    return attr;
}

class PausedAppender final : public LogAppender
{
public:
    void append(const std::string&) override
    {
        gate.enterAndWait();
        ++count;
    }
    Gate gate;
    std::atomic<int> count{0};
};

void ShutdownRetryScenario()
{
    InitializeAppConfig();
    auto& manager = LogManager::GetInstance();
    auto config = StdoutConfig();
    config.async.queue_capacity = 2;
    ASSERT_TRUE(manager.initialize(config).ok());
    auto logger = std::make_shared<Logger>(&manager, "shutdown-retry");
    auto appender = std::make_shared<PausedAppender>();
    appender->setLevel(LogLevel::DEBUG);
    logger->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);
    struct Cleanup {
        LogManager& manager;
        std::shared_ptr<PausedAppender> appender;
        ~Cleanup() { appender->gate.release(); (void)manager.shutdown(); }
    } cleanup{manager, appender};
    ASSERT_TRUE(manager.submitForAsync(Attr(logger, "first")).ok());
    ASSERT_TRUE(appender->gate.waitForEntered());
    ASSERT_TRUE(manager.submitForAsync(Attr(logger, "second")).ok());
    const auto first = manager.shutdown();
    EXPECT_FALSE(first.ok());
    EXPECT_EQ(manager.state(), LogManagerState::kStopAccepting);
    EXPECT_EQ(appender->count.load(), 0);
    EXPECT_EQ(manager.submitForAsync(Attr(logger, "late")).status,
        LogManagerResultStatus::kInvalidState);
    // 同时有两个 shutdown 控制者，生命周期锁应保证回收动作串行且幂等。
    auto left = std::async(std::launch::async, [&manager] { return manager.shutdown(); });
    auto right = std::async(std::launch::async, [&manager] { return manager.shutdown(); });
    appender->gate.release();
    EXPECT_TRUE(left.get().ok());
    EXPECT_TRUE(right.get().ok());
    EXPECT_EQ(appender->count.load(), 2);
    EXPECT_EQ(manager.state(), LogManagerState::kStopped);
}

void FileShutdownScenario()
{
    InitializeAppConfig("mmap_buffered");
    TempDirectory directory;
    const auto active = directory.file("active.log");
    auto& manager = LogManager::GetInstance();
    auto config = StdoutConfig();
    config.file.backend = LogFileBackendType::kMmapBuffered;
    config.loggers[0].appenders[0].type = LogAppenderType::kFile;
    config.loggers[0].appenders[0].file_path = active;
    ASSERT_TRUE(manager.initialize(config).ok());
    for (const auto& path : MmapSlotPaths::MakePaths(active).path)
    {
        EXPECT_TRUE(std::filesystem::exists(path)) << "shutdown test must exercise mmap: " << path;
    }
    auto logger = manager.getRootLogger();
    std::string expected;
    for (int i = 0; i < 20; ++i)
    {
        const auto line = "record-" + std::to_string(i);
        EXPECT_TRUE(manager.submitForAsync(Attr(logger, line)).ok());
        expected += line + "\n";
    }
    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_EQ(manager.state(), LogManagerState::kStopped);
    EXPECT_EQ(ReadFile(active), expected);
    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_EQ(manager.submitForAsync(Attr(logger, "late")).status,
        LogManagerResultStatus::kInvalidState);
}

void StdoutShutdownScenario()
{
    InitializeAppConfig();
    auto& manager = LogManager::GetInstance();
    ASSERT_TRUE(manager.initialize(StdoutConfig()).ok());
    auto logger = manager.getRootLogger();
    ASSERT_TRUE(manager.submitForAsync(Attr(logger, "stdout-only-marker")).ok());
    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_TRUE(manager.healthSnapshot().sinks.empty());
    EXPECT_EQ(manager.state(), LogManagerState::kStopped);
}

void ProducerBarrierScenario()
{
    InitializeAppConfig();
    auto& manager = LogManager::GetInstance();
    ASSERT_TRUE(manager.initialize(StdoutConfig()).ok());
    LogSubmissionGuard admitted(manager);
    ASSERT_TRUE(admitted.entered());
    auto stopping = std::async(std::launch::async, [&manager] { return manager.shutdown(); });
    Defer leave_on_failure([&admitted] { admitted.leave(); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (manager.state() == LogManagerState::kRunning && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    ASSERT_EQ(manager.state(), LogManagerState::kStopAccepting);
    EXPECT_EQ(stopping.wait_for(20ms), std::future_status::timeout);
    EXPECT_FALSE(LogSubmissionGuard(manager).entered());
    EXPECT_EQ(manager.submitForAsync(Attr(manager.getRootLogger(), "late")).status,
        LogManagerResultStatus::kInvalidState);
    admitted.leave();
    EXPECT_TRUE(stopping.get().ok());
    EXPECT_EQ(manager.state(), LogManagerState::kStopped);
}
void BackendFactoryScenario(bool use_mmap)
{
    InitializeAppConfig(use_mmap ? "mmap_buffered" : "");
    // 默认 regular 场景故意不创建 mmap 池，验证工厂不会误访问 mmapScheduler。
    LogCompressCoordinator coordinator(
        use_mmap ? std::make_unique<MmapBatchDispatcher>() : nullptr,
        std::make_unique<FullRecompressWorker>(FullRecompressWorker::Options{}));
    auto backend = LogFileBackend::NewDefaultBackend(coordinator);
    ASSERT_NE(backend, nullptr);
    if (use_mmap)
    {
        EXPECT_NE(dynamic_cast<MmapBufferedLogFileBackend*>(backend.get()), nullptr);
    }
    else
    {
        EXPECT_NE(dynamic_cast<RegularLogFileBackend*>(backend.get()), nullptr);
    }
}

void ManualFlushBoundaryScenario()
{
    InitializeAppConfig("regular");
    TempDirectory directory;
    const auto active = directory.file("active.log");
    auto config = StdoutConfig();
    config.loggers[0].appenders[0].type = LogAppenderType::kFile;
    config.loggers[0].appenders[0].file_path = active;
    auto& manager = LogManager::GetInstance();
    ASSERT_TRUE(manager.initialize(config).ok());
    auto paused = std::make_shared<PausedAppender>();
    auto blocker = std::make_shared<Logger>(&manager, "blocker");
    blocker->addAppender(paused);
    struct Cleanup {
        LogManager& manager;
        std::shared_ptr<PausedAppender> paused;
        ~Cleanup() { paused->gate.release(); (void)manager.shutdown(); }
    } cleanup{manager, paused};
    ASSERT_TRUE(manager.submitForAsync(Attr(blocker, "block")).ok());
    ASSERT_TRUE(paused->gate.waitForEntered());
    ASSERT_TRUE(manager.submitForAsync(Attr(manager.getRootLogger(), "queued")).ok());
    auto flushing = std::async(std::launch::async, [&manager, &active] { return manager.flush(active); });
    const auto ready = flushing.wait_for(1s);
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_EQ(ReadFile(active), "");
    paused->gate.release();
    EXPECT_TRUE(flushing.get().ok());
    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_EQ(ReadFile(active), "queued\n");
}

void StartupDeadlineScenario()
{
    InitializeAppConfig("regular");
    TempDirectory directory;
    const auto active = directory.file("existing.log");
    WriteFile(active, "existing");
    auto config = StdoutConfig();
    config.file.flush_interval_ms = 30000;
    config.loggers[0].appenders[0].type = LogAppenderType::kFile;
    config.loggers[0].appenders[0].file_path = active;
    auto& manager = LogManager::GetInstance();
    const auto before = TimeStamp::MonotonicNowMs();
    ASSERT_TRUE(manager.initialize(config).ok());
    Defer shutdown([&manager] { (void)manager.shutdown(); });
    const auto after = TimeStamp::MonotonicNowMs();
    auto sink = manager.acquireFileSink(active);
    ASSERT_NE(sink, nullptr);
    const auto deadline = sink->nextFlushTime();
    ASSERT_TRUE(deadline);
    EXPECT_GE(*deadline, before + 30000);
    EXPECT_LE(*deadline, after + 30000);
    EXPECT_EQ(sink->currentFileSize(), 8U);
}
} // namespace

// 测试思路：冷启动的非空 active 在打开时建立期限，必须使用本次配置而非注册器默认值。
// 例：配置 30s，8B 旧文件的 deadline 落在 initialize 起止时刻 +30s 内，而非默认 +3s。
TEST(LogLifecycleDeathTest, ExistingActiveUsesConfiguredIntervalDuringPreparation)
{
    EXPECT_EXIT({ StartupDeadlineScenario(); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：暂停上游 writer，再提交文件日志，主动 flush 只同步已交给 sink 的数据。
// 例：queued 尚在队列时 flush 返回且 active 为空；放行并 shutdown 后才出现 queued。
TEST(LogLifecycleDeathTest, ManualFlushDoesNotDrainUpstreamQueue)
{
    EXPECT_EXIT({ ManualFlushBoundaryScenario(); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：独立子进程隔离不可重启的 manager 单例，阻塞 writer 迫使首次 shutdown 超时。
// 例：两条已受理日志仍存活，late 被拒绝，放行后两个并发 shutdown 均成功且计数=2。
TEST(LogLifecycleDeathTest, WriterTimeoutRetainsRuntimeAndConcurrentRetryJoinsOnce)
{
    EXPECT_EXIT({ ShutdownRetryScenario(); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：最后一批日志未显式 flush，shutdown 必须先 drain writer，再 flush mmap，再释放资源。
// 例：20 条短记录小于 1MiB，退出后 active 精确包含 record-0..19，各一行。
TEST(LogLifecycleDeathTest, ShutdownFlushesFinalMmapTailAndRejectsLateProducer)
{
    EXPECT_EXIT({ FileShutdownScenario(); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：stdout-only 配置无需文件 sink，也必须走异步 writer 并可正常退出。
// 例：提交 marker 后 shutdown 成功，healthSnapshot.sinks 为空。
TEST(LogLifecycleDeathTest, StdoutOnlyConfigurationShutsDownWithoutFileSinks)
{
    EXPECT_EXIT({ StdoutShutdownScenario(); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：通过既有 friend 接缝暂停已进入计数区的 producer，shutdown 必须等计数归零。
// 例：admitted=1 -> shutdown 等待且拒绝 late -> leave -> shutdown 成功；不依赖暂停 writer 来伪造屏障。
TEST(LogLifecycleDeathTest, ShutdownWaitsForAdmittedProducerAndClosesNewAdmission)
{
    EXPECT_EXIT({ ProducerBarrierScenario(); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：独立子进程使用应用默认配置，工厂应选 regular，且无需 mmap 池。
// 例：未配置 backend、coordinator 的 mmap=nullptr，仍返回 RegularLogFileBackend。
TEST(LogLifecycleDeathTest, BackendFactoryDefaultsToRegularWithoutMmapPool)
{
    EXPECT_EXIT({ BackendFactoryScenario(false); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}

// 测试思路：通过真实 YAML 初始化显式选择 mmap，验证配置到工厂的完整接线。
// 例：file.backend=mmap_buffered，返回对象的实际类型为 MmapBufferedLogFileBackend。
TEST(LogLifecycleDeathTest, BackendFactoryHonorsExplicitMmapConfiguration)
{
    EXPECT_EXIT({ BackendFactoryScenario(true); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
        testing::ExitedWithCode(0), "");
}
