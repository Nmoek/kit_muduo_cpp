// Linux 专项故障注入，仅此测试可执行文件导出下列 POSIX 符号；不改生产接口。
// 通过 syscall 直接执行真实 I/O，只有显式作用域中的精确路径/操作会失败一次。
#include "base/log_mmap_completion.h"
#include "base/log.h"
#include "base/log_async.h"
#include "base/regular_log_file_backend.h"
#include "base/log_full_recompress.h"
#include "log_test_support.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

using namespace kit_muduo;
using namespace log_test;
using namespace std::chrono_literals;

namespace {
enum class Operation { kWrite, kOpen, kClose, kSync, kMmapSync, kRename, kThread, kUnlink, kEvent };
struct IoFaultState
{
    IoFaultState(Operation operation, std::string path, int error, int occurrence)
        : operation(operation), path(std::move(path)), error(error), occurrence(occurrence) {}
    bool matches(Operation candidate, const char* candidate_path)
    {
        if (candidate != operation || (candidate != Operation::kMmapSync && path != candidate_path)) return false;
        last_tid.store(::syscall(SYS_gettid));
        return ++calls == occurrence;
    }
    Operation operation;
    const std::string path;
    int error;
    int occurrence;
    // -1 表示正常写；0/正数模拟 write 返回 0 或短写。
    ssize_t short_write{-1};
    Gate* gate{nullptr};
    std::atomic<int> calls{0};
    std::atomic<long> last_tid{0};
    std::atomic<long> sync_size{-1};
    std::function<void()> before_sync;
};

std::shared_ptr<IoFaultState> current_fault;

class IoFault
{
public:
    IoFault(Operation operation, std::string path, int error, int occurrence = 1)
        : state(std::make_shared<IoFaultState>(operation, std::move(path), error, occurrence))
    {
        std::atomic_store(&current_fault, state);
    }
    ~IoFault() { std::atomic_store(&current_fault, std::shared_ptr<IoFaultState>{}); }
    // wrapper 先取得强引用，作用域退出不会使并行 I/O 访问已销毁的注入状态。
    std::shared_ptr<IoFaultState> state;
};

bool MatchFd(const std::shared_ptr<IoFaultState>& fault, Operation operation, int fd)
{
    if (!fault || operation != fault->operation) return false;
    char link[64], path[4096];
    std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    const auto size = ::syscall(SYS_readlinkat, AT_FDCWD, link, path, sizeof(path) - 1);
    if (size < 0) return false;
    path[size] = '\0';
    return fault->matches(operation, path);
}
} // namespace

extern "C" ssize_t write(int fd, const void* data, size_t size)
{
    auto fault = std::atomic_load(&current_fault);
    if (MatchFd(fault, Operation::kWrite, fd))
    {
        if (fault->short_write >= 0)
            return ::syscall(SYS_write, fd, data, std::min(size, static_cast<size_t>(fault->short_write)));
        if (fault->error) { errno = fault->error; return -1; }
    }
    return ::syscall(SYS_write, fd, data, size);
}

extern "C" int open(const char* path, int flags, ...)
{
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE)
    {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kOpen, path))
    {
        if (fault->gate) fault->gate->enterAndWait();
        if (fault->error) { errno = fault->error; return -1; }
    }
    return ::syscall(SYS_openat, AT_FDCWD, path, flags, mode);
}

extern "C" int close(int fd)
{
    auto fault = std::atomic_load(&current_fault);
    const bool fail = MatchFd(fault, Operation::kClose, fd);
    const int result = ::syscall(SYS_close, fd);
    // Linux close 报错时 fd 仍视为已释放，不能用“不 close”模拟而泄漏真实 fd。
    if (fail && fault->error) { errno = fault->error; return -1; }
    return result;
}

extern "C" int fdatasync(int fd)
{
    auto fault = std::atomic_load(&current_fault);
    if (MatchFd(fault, Operation::kSync, fd))
    {
        if (fault->before_sync) fault->before_sync();
        struct stat metadata{};
        if (::fstat(fd, &metadata) == 0) fault->sync_size.store(metadata.st_size);
        if (fault->error) { errno = fault->error; return -1; }
    }
    return ::syscall(SYS_fdatasync, fd);
}

extern "C" int epoll_create1(int flags) noexcept
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kEvent, "epoll_create1"))
    { errno = fault->error; return -1; }
    return ::syscall(SYS_epoll_create1, flags);
}

extern "C" int eventfd(unsigned int value, int flags) noexcept
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kEvent, "eventfd"))
    { errno = fault->error; return -1; }
    return ::syscall(SYS_eventfd2, value, flags);
}

extern "C" int timerfd_create(int clock, int flags) noexcept
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kEvent, "timerfd_create"))
    { errno = fault->error; return -1; }
    return ::syscall(SYS_timerfd_create, clock, flags);
}

extern "C" int epoll_ctl(int epoll, int op, int fd, epoll_event* event) noexcept
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kEvent, "epoll_ctl"))
    { errno = fault->error; return -1; }
    return ::syscall(SYS_epoll_ctl, epoll, op, fd, event);
}

extern "C" int msync(void* address, size_t size, int flags)
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kMmapSync, "") && fault->error)
    {
        errno = fault->error;
        return -1;
    }
    return ::syscall(SYS_msync, address, size, flags);
}

extern "C" int rename(const char* from, const char* to) noexcept
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kRename, to) && fault->error)
    {
        errno = fault->error;
        return -1;
    }
    return ::syscall(SYS_renameat, AT_FDCWD, from, AT_FDCWD, to);
}

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes,
    void* (*start)(void*), void* argument) noexcept
{
    using Create = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    static const auto real_create = reinterpret_cast<Create>(::dlsym(RTLD_NEXT, "pthread_create"));
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kThread, "") && fault->error) return fault->error;
    return real_create ? real_create(thread, attributes, start, argument) : EAGAIN;
}

extern "C" int unlink(const char* path) noexcept
{
    auto fault = std::atomic_load(&current_fault);
    if (fault && fault->matches(Operation::kUnlink, path) && fault->error)
    {
        errno = fault->error;
        return -1;
    }
    return ::syscall(SYS_unlinkat, AT_FDCWD, path, 0);
}

namespace {
class RejectedFull final : public FullRecompressScheduler
{
public:
    bool submit(FullRecompressTask task) noexcept override { tasks.push_back(std::move(task)); return false; }
    std::string compressSuffix() const noexcept override { return "zst"; }
    bool archiveInUse(const std::string&) const noexcept override { return false; }
    std::vector<FullRecompressTask> tasks;
};

class ObservedScheduler final : public MmapBatchScheduler
{
public:
    bool submitBatch(MmapSealedBatch::Ptr batch) noexcept override
    {
        last_batch = batch;
        return dispatcher.submitBatch(std::move(batch));
    }
    bool submitBatchEnd(MmapBatchEndTask task) noexcept override
    {
        ++ends;
        return dispatcher.submitBatchEnd(std::move(task));
    }
    bool submitActiveRotate(MmapActiveRotateTask task) noexcept override
    {
        return dispatcher.submitActiveRotate(std::move(task));
    }
    MmapSealedBatch::Ptr last_batch;
    MmapBatchDispatcher dispatcher;
    size_t ends{0};
};

class LogIoFaultTest : public testing::Test
{
protected:
    void SetUp() override
    {
        active = directory.file("active.log");
        archive = directory.file("archive.log");
        scheduler.dispatcher.start();
        backend = std::make_unique<MmapBufferedLogFileBackend>(scheduler, full,
            MmapBackendOptions{64, 8, true, 1000ms});
        ASSERT_TRUE(backend->open(active).ok());
    }
    void TearDown() override
    {
        if (backend->isOpen()) (void)backend->flush();
        scheduler.dispatcher.stopAccepting();
        scheduler.dispatcher.wait();
        backend->pollArchiveCompletions();
        backend->close();
        backend.reset();
    }
    LogFileRotateRequest request() const
    {
        return {backend->generation(), backend->lastSequence(), active, archive, true};
    }
    void finishArchive()
    {
        ASSERT_TRUE(scheduler.dispatcher.drain(3000));
        backend->pollArchiveCompletions();
    }
    TempDirectory directory;
    ObservedScheduler scheduler;
    RejectedFull full;
    std::unique_ptr<MmapBufferedLogFileBackend> backend;
    std::string active, archive;
};
} // namespace

// 测试思路：初始线程数=2，第二次 pthread_create 注入 EAGAIN；已启动线程必须 join，接收门不能开启。
// 例：start 抛 system_error，trySubmit=false，重复 start 也拒绝，join 可重复调用。
TEST(LogExecutorFaultTest, PartialStartupFailureClosesInstanceAndReapsWorkers)
{
    LogTaskExecutor executor({2, 2, 4, 1, "log-fail"});
    IoFault fault(Operation::kThread, "", EAGAIN, 2);
    EXPECT_THROW(executor.start(), std::system_error);
    EXPECT_EQ(fault.state->calls.load(), 2);
    EXPECT_FALSE(executor.trySubmit([] {}));
    EXPECT_THROW(executor.start(), std::logic_error);
    executor.join();
    executor.join();
    EXPECT_TRUE(executor.drain(0));
}

// 测试思路：任务已经入队后 CACHE 扩容失败，trySubmit 仍须返回 true，由已有 worker 消费且仅一次。
// 例：A 阻塞 -> B 入队 -> 创建第二线程 EAGAIN -> 释放 A -> B 计数=1。
TEST(LogExecutorFaultTest, GrowthFailureDoesNotRejectAlreadyAcceptedTask)
{
    Gate gate;
    std::atomic<int> completed{0};
    LogTaskExecutor executor({1, 2, 4, 1, "log-fail"});
    ReleaseGateOnExit release(gate);
    executor.start();
    ASSERT_TRUE(executor.trySubmit([&gate] { gate.enterAndWait(); }));
    ASSERT_TRUE(gate.waitForEntered());
    {
        IoFault fault(Operation::kThread, "", EAGAIN);
        EXPECT_TRUE(executor.trySubmit([&completed] { ++completed; }));
        EXPECT_EQ(fault.state->calls.load(), 1);
    }
    gate.release();
    executor.join();
    EXPECT_EQ(completed.load(), 1);
}

// 测试思路：seal 的两次 msync 任意一次失败都必须返回 false；例：第二次 header 同步 EIO 后原 payload 可重试。
TEST(LogSlotIoFaultTest, CommitReportsBothPayloadAndHeaderSyncFailures)
{
    for (int occurrence : {1, 2})
    {
        TempDirectory directory;
        MmapSlot slot;
        ASSERT_TRUE(slot.open(directory.file("slot.mmap"), 64).ok());
        ASSERT_TRUE(slot.begin(1, 1));
        const std::string payload = "payload";
        ASSERT_TRUE(slot.append({reinterpret_cast<const uint8_t*>(payload.data()), payload.size()}));
        {
            IoFault fault(Operation::kMmapSync, "", EIO, occurrence);
            EXPECT_FALSE(slot.seal());
            EXPECT_EQ(fault.state->calls.load(), occurrence);
        }
        EXPECT_EQ(slot.header().payload_size, payload.size());
        EXPECT_TRUE(slot.seal());
    }
}

// 测试思路：reset 的 header 同步失败不得报告成功；例：EIO 返回 false，重试同步成功后 epoch=10。
TEST(LogSlotIoFaultTest, ResetReportsSyncFailure)
{
    TempDirectory directory;
    MmapSlot slot;
    ASSERT_TRUE(slot.open(directory.file("slot.mmap"), 64).ok());
    {
        IoFault fault(Operation::kMmapSync, "", EIO);
        EXPECT_FALSE(slot.reset(9));
        EXPECT_EQ(fault.state->calls.load(), 1);
    }
    EXPECT_TRUE(slot.reset(10));
    EXPECT_EQ(slot.header().slot_epoch, 10U);
}

// 测试思路：真实 write 返回 EINTR 或短写时必须继续剩余字节；例：8B 首次仅写 2B，最终仍恰好 8B。
TEST_F(LogIoFaultTest, ActiveWriteRetriesInterruptAndShortWrite)
{
    for (bool short_write : {false, true})
    {
        IoFault fault(Operation::kWrite, active, short_write ? 0 : EINTR);
        if (short_write) fault.state->short_write = 2;
        ASSERT_TRUE(backend->append("12345678").ok());
        ASSERT_TRUE(backend->flush().ok());
        EXPECT_EQ(fault.state->calls.load(), 2);
    }
    EXPECT_EQ(ReadFile(active), "1234567812345678");
}

// 测试思路：ENOSPC 真正发生在 active worker 写文件处，必须保留已提交 slot，拒绝 flush/rotate。
TEST_F(LogIoFaultTest, ActiveDiskFullPreservesCommittedPayload)
{
    IoFault fault(Operation::kWrite, active, ENOSPC);
    ASSERT_TRUE(backend->append("12345678").ok());
    EXPECT_FALSE(backend->flush().ok());
    ASSERT_TRUE(scheduler.dispatcher.drain(3000));
    EXPECT_EQ(fault.state->calls.load(), 1);
    EXPECT_FALSE(backend->rotate(request(), 1000).ok());
    EXPECT_FALSE(std::filesystem::exists(archive));
    MmapSlot slot;
    ASSERT_TRUE(slot.open(MmapSlotPaths::MakePaths(active).path[0], 64).ok());
    EXPECT_EQ(slot.header().payload_size, 8U);
    EXPECT_EQ(static_cast<MmapSlotState>(slot.header().state), MmapSlotState::kProcessing);
}

// 测试思路：flush 先完成双分支，再同步 active；例：fdatasync 入口两分支终态且 fstat 大小已是 4B。
TEST_F(LogIoFaultTest, FlushSyncsActiveAfterBothBranchesFinish)
{
    ASSERT_TRUE(backend->append("tail").ok());
    IoFault fault(Operation::kSync, active, 0);
    bool both_terminal_at_sync = false;
    fault.state->before_sync = [this, &both_terminal_at_sync] {
        both_terminal_at_sync = scheduler.last_batch && scheduler.last_batch->completion().bothTerminal();
    };
    ASSERT_TRUE(backend->flush().ok());
    EXPECT_TRUE(both_terminal_at_sync);
    EXPECT_EQ(fault.state->calls.load(), 1);
    EXPECT_EQ(fault.state->sync_size.load(), 4);
    EXPECT_EQ(ReadFile(active), "tail");
}

// 测试思路：fdatasync EIO 必须透传为失败，不能把 write 已完成等同于同步成功；例：下次同步可重试。
TEST_F(LogIoFaultTest, FlushReportsSyncErrorAndCanRetry)
{
    ASSERT_TRUE(backend->append("tail").ok());
    {
        IoFault fault(Operation::kSync, active, EIO);
        EXPECT_FALSE(backend->flush().ok());
        EXPECT_EQ(fault.state->calls.load(), 1);
    }
    EXPECT_TRUE(backend->flush().ok());
    EXPECT_EQ(ReadFile(active), "tail");
}

// 测试思路：旧 active 同步失败必须发生在关闭和 rename 之前，重试不重复追加或推进代次。
// 例：old 的轮转同步注入 EIO，仍保留 active/原代；解除故障后归档为 old。
TEST_F(LogIoFaultTest, RotationSyncFailureRetainsActiveAndGeneration)
{
    ASSERT_TRUE(backend->append("old").ok());
    const auto generation = backend->generation();
    {
        IoFault fault(Operation::kSync, active, EIO);
        EXPECT_FALSE(backend->rotate(request(), 1000).ok());
        EXPECT_TRUE(backend->isOpen());
        EXPECT_EQ(backend->generation(), generation);
        EXPECT_TRUE(std::filesystem::exists(active));
        EXPECT_FALSE(std::filesystem::exists(archive));
        EXPECT_EQ(ReadFile(active), "old");
    }
    ASSERT_TRUE(backend->rotate(request(), 1000).ok());
    finishArchive();
    EXPECT_EQ(DecompressFile(archive + ".zst"), "old");
}

// 测试思路：regular 的 flush 必须真实调用 fdatasync，错误透传且不关闭文件，轮转也受此保护。
// 例：写入 old，首次同步 EIO；再次同步观察到 3B，轮转同步失败时原路径仍在。
TEST(LogRegularFlushTest, SyncAndRotationUseDurableActiveBoundary)
{
    TempDirectory directory;
    const auto path = directory.file("regular.log");
    RejectedFull full;
    RegularLogFileBackend backend(full);
    ASSERT_TRUE(backend.open(path).ok());
    ASSERT_TRUE(backend.append("old").ok());
    {
        IoFault fault(Operation::kSync, path, EIO);
        EXPECT_EQ(backend.flush().status, LogBackendStatus::kSyncFailed);
        EXPECT_TRUE(backend.isOpen());
    }
    {
        IoFault fault(Operation::kSync, path, 0);
        ASSERT_TRUE(backend.flush().ok());
        EXPECT_EQ(fault.state->calls.load(), 1);
        EXPECT_EQ(fault.state->sync_size.load(), 3);
    }
    const auto archive = directory.file("archive.log");
    {
        IoFault fault(Operation::kSync, path, EIO);
        EXPECT_FALSE(backend.rotate({backend.generation(), backend.lastSequence(), path, archive, false}).ok());
        EXPECT_FALSE(std::filesystem::exists(archive));
        EXPECT_TRUE(backend.isOpen());
    }
    EXPECT_TRUE(backend.rotate({backend.generation(), backend.lastSequence(), path, archive, false}).ok());
    EXPECT_EQ(ReadFile(archive), "old");
    backend.close();
}

// 测试思路：逐阶段注入事件初始化故障，检查已创建 fd 均释放，不启动无定时能力的 writer。
// 例：eventfd 已创建后 epoll 失败、timerfd 失败或第二个 epoll_ctl 失败，fd 数量回到原值。
TEST(LogWriterEventFaultTest, FailedInitializationReleasesAllDescriptors)
{
    const auto fd_count = [] {
        size_t count = 0;
        for (const auto& item : std::filesystem::directory_iterator("/proc/self/fd"))
        { (void)item; ++count; }
        return count;
    };
    for (const auto& name : {"eventfd", "epoll_create1", "timerfd_create", "epoll_ctl"})
    {
        SCOPED_TRACE(name);
        const auto before = fd_count();
        {
            LogFileSinkRegister registry;
            LogAsyncDispatcher dispatcher(registry);
            IoFault fault(Operation::kEvent, name, EMFILE, std::string(name) == "epoll_ctl" ? 2 : 1);
            EXPECT_THROW(dispatcher.start(), std::system_error);
            EXPECT_THROW(dispatcher.start(), std::logic_error);
        }
        EXPECT_EQ(fd_count(), before);
    }
}

// 测试思路：实际 batch 提交和 fdatasync 分别计数，验证两个阈值无需大小关系。
// 例：batch=4B/sink=8B 时第一批只提交；batch=16B/sink=8B 时由 flush 封口尾批。
TEST(LogFlushIntegrationTest, BatchSubmissionAndSinkSyncHaveIndependentThresholds)
{
    for (size_t batch_bytes : {4U, 16U})
    {
        SCOPED_TRACE(batch_bytes);
        TempDirectory directory;
        const auto active = directory.file("active.log");
        ObservedScheduler scheduler;
        scheduler.dispatcher.start();
        RejectedFull full;
        LogFileSinkRegister registry;
        LogFileConfig config;
        config.flush_threshold = 8;
        registry.setConfig(config);
        LogFileSink sink(&registry, active, std::make_unique<MmapBufferedLogFileBackend>(
            scheduler, full, MmapBackendOptions{64, batch_bytes, false, 1000ms}));
        ASSERT_TRUE(sink.ensureOpen().ok());
        IoFault sync(Operation::kSync, active, 0);
        ASSERT_TRUE(sink.append("1234").ok());
        EXPECT_EQ(bool(scheduler.last_batch), batch_bytes == 4);
        EXPECT_EQ(sync.state->calls.load(), 0);
        const auto result = sink.append("5678");
        EXPECT_TRUE(result.ok());
        EXPECT_TRUE(result.flush_attempted);
        EXPECT_EQ(sync.state->calls.load(), 1);
        EXPECT_FALSE(sink.nextFlushTime());
        EXPECT_EQ(ReadFile(active), "12345678");
        scheduler.dispatcher.stopAccepting();
        scheduler.dispatcher.wait();
        sink.close();
    }
}

// 测试思路：真实 committed slot 冷恢复后无需新日志，由空闲 writer 对恢复后的 active 同步。
// 例：prefix + sealed(tail) -> open 回放 -> timerfd -> fdatasync 观察到完整 10B。
TEST(LogFlushIntegrationTest, RecoveredTailIsAutomaticallySyncedWithoutNewLogs)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    WriteFile(active, "prefix");
    {
        MmapSlot slot;
        ASSERT_TRUE(slot.open(MmapSlotPaths::MakePaths(active).path[0], 64).ok());
        ASSERT_TRUE(slot.begin(7, 1));
        const std::string tail = "tail";
        ASSERT_TRUE(slot.append({reinterpret_cast<const uint8_t*>(tail.data()), tail.size()}));
        ASSERT_TRUE(slot.seal());
    }
    ObservedScheduler scheduler;
    scheduler.dispatcher.start();
    RejectedFull full;
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.flush_interval_ms = 20;
    registry.setConfig(config);
    auto sink = std::make_shared<LogFileSink>(&registry, active,
        std::make_unique<MmapBufferedLogFileBackend>(scheduler, full,
            MmapBackendOptions{64, 8, true, 1000ms}));
    ASSERT_TRUE(sink->ensureOpen().ok());
    ASSERT_EQ(sink->currentFileSize(), 10U);
    registry.commit({{active, sink}});
    Gate syncing;
    IoFault sync(Operation::kSync, active, 0);
    sync.state->before_sync = [&syncing] { syncing.enterAndWait(); };
    LogAsyncDispatcher writer(registry);
    ReleaseGateOnExit release(syncing);
    writer.start();
    EXPECT_TRUE(syncing.waitForEntered());
    syncing.release();
    EXPECT_TRUE(writer.shutdown());
    writer.join();
    EXPECT_EQ(sync.state->calls.load(), 1);
    EXPECT_EQ(sync.state->sync_size.load(), 10);
    EXPECT_EQ(ReadFile(active), "prefixtail");
    EXPECT_FALSE(sink->nextFlushTime());
    scheduler.dispatcher.stopAccepting();
    scheduler.dispatcher.wait();
    sink->close();
}

namespace {
void ShutdownSyncRetryScenario(bool mmap)
{
    InitializeAppConfig(mmap ? "mmap_buffered" : "regular");
    TempDirectory directory;
    const auto active = directory.file("active.log");
    auto config = DefaultLogConfig();
    config.file.backend = mmap ? LogFileBackendType::kMmapBuffered : LogFileBackendType::kRegular;
    config.file.flush_interval_ms = 30000;
    config.loggers[0].formatter = "%m%n";
    config.loggers[0].appenders = {{LogAppenderType::kFile, LogLevel::DEBUG, "%m%n", active}};
    auto& manager = LogManager::GetInstance();
    ASSERT_TRUE(manager.initialize(config).ok());
    auto logger = manager.getRootLogger();
    auto record = std::make_shared<LogAttr>(logger, LogLevel::INFO, logger->getName(),
        "test", __FILE__, __LINE__, 0, ::pthread_self(), ::getpid(), "test", 0);
    record->getSS() << "tail";
    record->seal();
    ASSERT_TRUE(manager.submitForAsync(record).ok());
    IoFault sync(Operation::kSync, active, EIO);
    EXPECT_FALSE(manager.shutdown().ok());
    EXPECT_EQ(manager.state(), LogManagerState::kStopAccepting);
    EXPECT_EQ(sync.state->calls.load(), 1);
    EXPECT_EQ(ReadFile(active), "tail\n");
    // 注入只在第一次调用失败；重试成功后不应再出现旧 durableFlush 造成的第二次同步。
    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_EQ(manager.state(), LogManagerState::kStopped);
    EXPECT_EQ(sync.state->calls.load(), 2);
    EXPECT_TRUE(manager.shutdown().ok());
    EXPECT_EQ(sync.state->calls.load(), 2);
    EXPECT_EQ(ReadFile(active), "tail\n");
}
} // namespace

// 测试思路：子进程隔离 manager 单例，最终同步失败后必须保留运行资源供重试。
// 例：regular/mmap 各一条尾日志，首次 shutdown=EIO，第二次成功，第三次不重复同步。
TEST(LogFlushLifecycleDeathTest, FinalSyncFailureRetainsRuntimeAndRetriesOnce)
{
    for (bool mmap : {false, true})
    {
        SCOPED_TRACE(mmap);
        EXPECT_EXIT({ ShutdownSyncRetryScenario(mmap); ::_exit(testing::Test::HasFailure() ? 1 : 0); },
            testing::ExitedWithCode(0), "");
    }
}

// 测试思路：rename 已成功而新 active open 失败，代次已经推进且 End 仍须执行；普通 open 只重开新代。
// 例：old 进入归档，EACCES 返回 kOpenFailed，重开写 new，End 总计一次。
TEST_F(LogIoFaultTest, ReopenFailureAfterRenameStillFinishesOldArchive)
{
    ASSERT_TRUE(backend->append("old").ok());
    const auto generation = backend->generation();
    {
        IoFault fault(Operation::kOpen, active, EACCES);
        EXPECT_EQ(backend->rotate(request(), 1000).status, LogBackendStatus::kOpenFailed);
        EXPECT_EQ(fault.state->calls.load(), 1);
    }
    EXPECT_EQ(backend->generation(), generation + 1);
    EXPECT_FALSE(backend->isOpen());
    finishArchive();
    EXPECT_EQ(scheduler.ends, 1U);
    EXPECT_EQ(DecompressFile(archive + ".zst"), "old");
    ASSERT_TRUE(backend->open(active).ok());
    ASSERT_TRUE(backend->append("new").ok());
    ASSERT_TRUE(backend->flush().ok());
    EXPECT_EQ(ReadFile(active), "new");
    EXPECT_EQ(scheduler.ends, 1U);
}

// 测试思路：rename 冲突且旧 fd 重开也失败，必须保留原文及两类错误，不发 End；普通 open 后仍属旧代。
TEST_F(LogIoFaultTest, RenameAndReopenFailurePreserveOldGeneration)
{
    WriteFile(archive, "existing");
    ASSERT_TRUE(backend->append("old").ok());
    const auto generation = backend->generation();
    {
        IoFault fault(Operation::kOpen, active, EACCES);
        const auto result = backend->rotate(request(), 1000);
        EXPECT_FALSE(result.ok());
        EXPECT_NE(result.message.find("reopen:"), std::string::npos);
    }
    EXPECT_EQ(backend->generation(), generation);
    EXPECT_EQ(scheduler.ends, 0U);
    EXPECT_EQ(ReadFile(archive), "existing");
    ASSERT_TRUE(backend->open(active).ok());
    ASSERT_TRUE(backend->append("new").ok());
    ASSERT_TRUE(backend->flush().ok());
    EXPECT_EQ(ReadFile(active), "oldnew");
}

// 测试思路：close 报 EIO 后 I/O 状态未知，不能继续 rename 或靠普通 open 清除故障。
// 例：active 原文保留，generation 不变，archive 不存在，End=0。
TEST_F(LogIoFaultTest, CloseFailureDoesNotRenameOrClearActiveFailure)
{
    ASSERT_TRUE(backend->append("old").ok());
    const auto generation = backend->generation();
    {
        IoFault fault(Operation::kClose, active, EIO);
        EXPECT_FALSE(backend->rotate(request(), 1000).ok());
        EXPECT_EQ(fault.state->calls.load(), 1);
    }
    EXPECT_EQ(backend->generation(), generation);
    EXPECT_FALSE(backend->open(active).ok());
    EXPECT_FALSE(std::filesystem::exists(archive));
    EXPECT_EQ(ReadFile(active), "old");
    EXPECT_EQ(scheduler.ends, 0U);
}

// 测试思路：压缩发布 rename 失败只保留原文，补偿时机交给 sink，backend 不立即提交。
// 例：rename=EIO、active 轮转成功，raw="old" 保留，重复 poll 后 full 提交次数仍为 0。
TEST_F(LogIoFaultTest, CompressionPublishFailureRetainsRawWithoutImmediateCompensation)
{
    ASSERT_TRUE(backend->append("old").ok());
    IoFault fault(Operation::kRename, archive + ".zst", EIO);
    ASSERT_TRUE(backend->rotate(request(), 1000).ok());
    finishArchive();
    EXPECT_EQ(fault.state->calls.load(), 1);
    EXPECT_TRUE(std::filesystem::exists(archive)) << "failed publication must retain raw text";
    EXPECT_FALSE(std::filesystem::exists(archive + ".zst"));
    EXPECT_EQ(backend->lastRotationReport().compression_error, std::errc::io_error);
    EXPECT_EQ(backend->lastRotationReport().disposition, MmapArchiveDisposition::kRawRetained);
    EXPECT_TRUE(full.tasks.empty());
    backend->pollArchiveCompletions();
    EXPECT_TRUE(full.tasks.empty());
    EXPECT_EQ(ReadFile(archive), "old");
    EXPECT_FALSE(std::filesystem::exists(archive + ".zst"));
    EXPECT_TRUE(backend->isOpen());
}

// 测试思路：第一次重名改发 .bck 后，备份发布失败也必须保留原文和既有 .zst。
// 例：.zst="keep"、raw="new"，对 .bck 的 rename 注入 EIO；两份原始内容不变，报告 RawRetained。
TEST_F(LogIoFaultTest, BackupPublishFailureRetainsRawAndOriginalArchive)
{
    WriteFile(archive + ".zst", "keep");
    ASSERT_TRUE(backend->append("new").ok());
    IoFault fault(Operation::kRename, archive + ".zst.bck", EIO);
    ASSERT_TRUE(backend->rotate(request(), 1000).ok());
    finishArchive();
    EXPECT_EQ(fault.state->calls.load(), 1);
    EXPECT_EQ(ReadFile(archive), "new");
    EXPECT_EQ(ReadFile(archive + ".zst"), "keep");
    EXPECT_FALSE(std::filesystem::exists(archive + ".zst.bck"));
    EXPECT_EQ(backend->lastRotationReport().compression_error, std::errc::io_error);
    EXPECT_EQ(backend->lastRotationReport().disposition, MmapArchiveDisposition::kRawRetained);
    EXPECT_TRUE(full.tasks.empty()) << "existing compressed archive must not be recompressed over";
}

// 测试思路：物理操作已开始后预算到期仍须等确定结果，不允许调用者返回后后台继续切换 fd。
// 例：rename 成功、open 门闩阻塞超过 100ms；rotate 仍等待，释放后成功且 open 在线程池执行。
TEST_F(LogIoFaultTest, DeadlineCannotEscapeStartedPhysicalRotation)
{
    ASSERT_TRUE(backend->append("old").ok());
    Gate gate;
    IoFault fault(Operation::kOpen, active, 0);
    fault.state->gate = &gate;
    std::atomic<long> caller_tid{0};
    const auto rotate_request = request();
    auto rotating = std::async(std::launch::async, [this, rotate_request, &caller_tid] {
        caller_tid = ::syscall(SYS_gettid);
        return backend->rotate(rotate_request, 100);
    });
    ReleaseGateOnExit release(gate);
    ASSERT_TRUE(gate.waitForEntered());
    EXPECT_EQ(rotating.wait_for(150ms), std::future_status::timeout);
    EXPECT_TRUE(std::filesystem::exists(archive));
    gate.release();
    EXPECT_TRUE(rotating.get().ok());
    EXPECT_NE(fault.state->last_tid.load(), caller_tid.load());
    finishArchive();
    EXPECT_EQ(DecompressFile(archive + ".zst"), "old");
}

// 测试思路：阻塞 full worker 打开第一个输入，第二任务填满队列，重复/第三任务均拒绝。
// 同时实时 mmap active/compression 两分支仍能推进，证明全量压缩不占用实时池线程。
// 例：full A(门闩)+B(排队)，C 保留原文；mmap="realtime" 在放行 A 前已完成 flush。
TEST_F(LogIoFaultTest, FullQueueDeduplicatesAndCannotStarveRealtimeWorkers)
{
    const auto first = directory.file("first.log");
    const auto second = directory.file("second.log");
    const auto third = directory.file("third.log");
    WriteFile(first, "first");
    WriteFile(second, "second");
    WriteFile(third, "third");
    const auto task = [](const std::string& path) {
        return FullRecompressTask{FullRecompressTask::NextTaskId(), 1, path, path + ".zst"};
    };
    Gate gate;
    FullRecompressWorker worker({1, 64});
    IoFault fault(Operation::kOpen, first, 0);
    fault.state->gate = &gate;
    ReleaseGateOnExit release(gate);
    worker.start();
    ASSERT_TRUE(worker.submit(task(first)));
    ASSERT_TRUE(gate.waitForEntered());
    EXPECT_FALSE(worker.drain(10));
    EXPECT_TRUE(worker.archiveInUse(first));
    EXPECT_TRUE(worker.archiveInUse(first + ".zst"));
    EXPECT_FALSE(worker.submit(task(first)));
    ASSERT_TRUE(worker.submit(task(second)));
    EXPECT_FALSE(worker.submit(task(third)));
    ASSERT_TRUE(backend->append("realtime").ok());
    ASSERT_TRUE(backend->flush().ok());
    ASSERT_TRUE(scheduler.dispatcher.drain(3000));
    EXPECT_EQ(ReadFile(active), "realtime");
    EXPECT_FALSE(worker.drain(0));
    worker.stopAccepting();
    gate.release();
    worker.wait();
    EXPECT_EQ(DecompressFile(first + ".zst"), "first");
    EXPECT_EQ(DecompressFile(second + ".zst"), "second");
    EXPECT_EQ(ReadFile(third), "third");
    EXPECT_FALSE(worker.archiveInUse(first));
    EXPECT_FALSE(worker.archiveInUse(second + ".zst"));
}

// 测试思路：压缩已发布而 raw unlink 失败，只记录清理错误，不能重新补偿或删除成功产物。
// 例：raw 和 .zst 都保留，报告 RealtimePublished/EACCES，重复 poll 不再发布或补偿。
TEST_F(LogIoFaultTest, RawUnlinkFailureKeepsPublishedArchiveWithoutRecompression)
{
    ASSERT_TRUE(backend->append("old").ok());
    IoFault fault(Operation::kUnlink, archive, EACCES);
    ASSERT_TRUE(backend->rotate(request(), 1000).ok());
    finishArchive();
    EXPECT_EQ(fault.state->calls.load(), 1);
    EXPECT_EQ(backend->lastRotationReport().disposition, MmapArchiveDisposition::kRealtimePublished);
    EXPECT_EQ(backend->lastRotationReport().cleanup_error, std::errc::permission_denied);
    EXPECT_EQ(ReadFile(archive), "old");
    EXPECT_EQ(DecompressFile(archive + ".zst"), "old");
    EXPECT_TRUE(full.tasks.empty());
    backend->pollArchiveCompletions();
    EXPECT_TRUE(full.tasks.empty());
    EXPECT_FALSE(backend->hasPendingArchives());
}
