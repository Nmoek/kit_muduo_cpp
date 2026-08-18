/**
 * @file log_benchmark.cpp
 * @brief 日志系统基准测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-11 01:41:32
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log.h"
#include "base/log_appender.h"
#include "base/util.h"

#include <benchmark/benchmark.h>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

using namespace kit_muduo;

namespace {

/// 用于固定测试次数
constexpr std::int64_t kPerfStatIterations = 100'000'000;

constexpr char kFilteredLoggerName[] = "bench.filtered";
constexpr char kFilteredLongLoggerName[] = "benchmark.filtered.long";

constexpr char kModuleLoggerName[] = "base";

// 操作次数总量不变的情况下 每个线程均摊的操作次数
constexpr int kPerfStatThreads = 8;
constexpr int64_t kPerfStatIterationsPerThread =
    kPerfStatIterations / kPerfStatThreads / kPerfStatThreads;

/// 完整日志路径包含 LogAttr 和 stringstream 构造，固定较小总工作量。
constexpr std::int64_t kFullPathPerfStatIterations = 1'250'000;
constexpr int64_t kFullPathPerfStatIterationsPerThread =
    kFullPathPerfStatIterations / kPerfStatThreads;

constexpr char kConsoleMessage[] =
    "discarded stdout message\n";

constexpr int64_t kConsoleMessageBytes =
    sizeof(kConsoleMessage) - 1;

Logger::Ptr PrepareFilteredModuleLogger()
{
    auto logger = LogManager::GetInstance().getLogger(kModuleLoggerName);

    logger->setLevel(LogLevel::ERROR);
    return logger;
}

} // namespace

/*
这个用例不是日志性能结果，只是测量框架底噪。

测试思路：
1. 只执行 Google Benchmark 的迭代循环和编译器优化屏障；
2. 不调用日志系统，用它测量 benchmark 框架自身的基础开销；
3. 后续日志用例的单次耗时必须明显高于这个基线，否则数据容易被框架开销污染。

示例：
BM_BaselineLoop 每轮只让编译器保留 value，不进行日志构造、格式化或 I/O。

测量范围：
Google Benchmark 迭代循环、DoNotOptimize 编译器屏障。

排除范围：
Logger、LogAttr、LogFormatter、Appender、文件 I/O 和锁竞争。
*/
void BM_BaselineLoop(benchmark::State& state)
{
    /*
        benchmark::State 控制运行次数和计时。
    */

    std::uint64_t value = 0;

    //    for(auto _ : state) 不是普通固定循环，框架会动态选择足够的迭代次数。
    for(auto _ : state)
    {

        /*
            (void)_; 避免当前项目的 -Wall -Werror 把循环变量判为未使用。
        */
        (void)_;
        /*
            DoNotOptimize(value) 防止编译器认为循环没有效果而整个删除。
        */
        benchmark::DoNotOptimize(value);
    }

    /*
        SetItemsProcessed() 让报告里包含处理条目吞吐。
    */
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BaselineLoop);


class DiscardAppender final : public LogAppender
{
public:
    void log(LogAttr::Ptr) override
    {
        // donothing
    }
    void log(const std::string &) override
    {
        // donothing
    }
};

class SimulatConsoleAppender final : public LogAppender
{
public:
    void log(LogAttr::Ptr attr) override
    {
        if(attr->getLevel() < level_)
            return;

        if(formatter_)
        {
            auto log_data = formatter_->format(attr);
            benchmark::DoNotOptimize(log_data);
            // 最终不输出
        }
    }

    void log(const std::string &log_data) override
    {
        benchmark::DoNotOptimize(log_data);
        std::lock_guard<std::mutex> lock(ConsoleAppender::GetConsoleMtx());
        // 最终不输出
    }
};



Logger::Ptr GetFilteredLogger()
{
    static auto logger = [] {
        auto logger = KIT_LOGGER(kFilteredLoggerName);

        logger->setLevel(LogLevel::ERROR);
        logger->addAppender(std::make_shared<DiscardAppender>());
        return logger;
    }();

    return logger;
}

Logger::Ptr GetFilteredLongNameLogger()
{
    static auto logger = [] {
        auto logger = KIT_LOGGER(kFilteredLongLoggerName);

        logger->setLevel(LogLevel::ERROR);
        logger->addAppender(std::make_shared<DiscardAppender>());
        return logger;
    }();

    return logger;
}

Logger::Ptr GetDiscardLogger()
{
    static auto logger = [] {
        auto logger = KIT_LOGGER(kModuleLoggerName);
        // 清空原来模块的输出器
        logger->clearAppender();

        logger->setLevel(LogLevel::DEBUG);
        logger->addAppender(std::make_shared<DiscardAppender>());
        return logger;
    }();

    return logger;
}


// 完全模拟ConsoleAppender 但不真的输出
Logger::Ptr GetSimulatConsoleLogger()
{
    static auto logger = [] {
        auto logger = KIT_LOGGER(kModuleLoggerName);
        // 清空原来模块的输出器
        logger->clearAppender();

        logger->setLevel(LogLevel::DEBUG);
        auto appender = std::make_shared<SimulatConsoleAppender>();
        appender->setLevel(LogLevel::DEBUG);
        logger->addAppender(std::move(appender));
        return logger;
    }();

    return logger;
}

Logger::Ptr GetConsoleLogger()
{
    static auto logger = [] {
        auto logger = KIT_LOGGER(kModuleLoggerName);
        // 清空原来模块的输出器
        logger->clearAppender();

        logger->setLevel(LogLevel::DEBUG);
        auto appender = std::make_shared<ConsoleAppender>();
        appender->setLevel(LogLevel::DEBUG);
        logger->addAppender(std::move(appender));
        return logger;
    }();

    return logger;
}

Logger::Ptr GetFileLogger()
{
    static auto logger = [] {
        auto logger = KIT_LOGGER(kModuleLoggerName);
        // 清空原来模块的输出器
        logger->clearAppender();

        logger->setLevel(LogLevel::DEBUG);
        const std::string file_path =
            std::string("/tmp/kit-log-bench/module-log-file-")
            + std::to_string(GetThreadPid()) + ".log";
        auto appender = std::make_shared<FileAppender>(
            LogManager::GetInstance().acquireFileSink(file_path));

        appender->setLevel(LogLevel::DEBUG);
        logger->addAppender(std::move(appender));
        return logger;
    }();

    return logger;
}

LogFileSinkRegister& GetDirectFileSinkRegister()
{
    static LogFileSinkRegister registry;
    return registry;
}

LogFileSink::Ptr GetSharedDirectFileSink()
{
    static auto sink = GetDirectFileSinkRegister().acquire(
        std::string("/tmp/kit-log-bench/direct-file-sink-shared-")
        + std::to_string(GetThreadPid()) + ".log");
    return sink;
}

LogFileSink::Ptr GetIndependentDirectFileSink(int thread_index)
{
    static const auto sinks = [] {
        std::array<LogFileSink::Ptr, kPerfStatThreads> result;
        const auto owner_thread_id = GetThreadPid();

        for(size_t index = 0; index < result.size(); ++index)
        {
            result[index] = GetDirectFileSinkRegister().acquire(
                std::string("/tmp/kit-log-bench/direct-file-sink-independent-")
                + std::to_string(owner_thread_id) + "-"
                + std::to_string(index) + ".log");
        }
        return result;
    }();

    return sinks.at(static_cast<size_t>(thread_index));
}

class FileAppenderBenchmarkRun
{
public:
    std::string begin(int thread_count, const std::string& variant)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        if(!run_active_)
        {
            run_active_ = true;
            thread_count_ = thread_count;
            arrived_ = 0;
            ready_ = 0;
            writes_finished_ = 0;
            flushed_ = false;
            finished_ = 0;
            records_per_thread_.assign(static_cast<size_t>(thread_count), 0);
            setup_error_.clear();
            shared_sink_ = nullptr;
            shared_appender_.reset();
            variant_ = variant;

            const char* configured_dir =
                std::getenv("KIT_LOG_BENCH_VALIDATION_DIR");
            const std::filesystem::path validation_dir = configured_dir
                ? std::filesystem::path(configured_dir)
                : std::filesystem::path("/tmp/kit-log-bench/validation");
            const std::string file_stem = variant_ + "-pid-"
                + std::to_string(::getpid()) + "-run-"
                + std::to_string(run_number_++);
            path_ = (validation_dir / (file_stem + ".log")).string();
            manifest_path_ =
                (validation_dir / (file_stem + ".manifest")).string();

            std::error_code error;
            std::filesystem::create_directories(
                std::filesystem::path(path_).parent_path(), error);
            if(error)
            {
                setup_error_ = "cannot create benchmark directory: "
                    + error.message();
            }
            else
            {
                std::filesystem::remove(path_, error);
                if(error)
                {
                    setup_error_ = "cannot reset validation file: "
                        + error.message();
                }
                error.clear();
                std::filesystem::remove(manifest_path_, error);
                if(error && setup_error_.empty())
                {
                    setup_error_ = "cannot reset validation manifest: "
                        + error.message();
                }
            }
        }

        if(thread_count_ != thread_count || variant_ != variant)
        {
            setup_error_ = "file-appender benchmark participant mismatch";
        }

        ++arrived_;
        if(arrived_ == thread_count_)
        {
            cv_.notify_all();
        }
        cv_.wait(lock, [this] { return arrived_ == thread_count_; });
        return path_;
    }

    void waitReady()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        ++ready_;
        if(ready_ == thread_count_)
        {
            cv_.notify_all();
            return;
        }
        cv_.wait(lock, [this] { return ready_ == thread_count_; });
    }

    void registerSink(const LogFileSink::Ptr& sink)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if(!shared_sink_)
        {
            shared_sink_ = sink.get();
            return;
        }
        if(shared_sink_ != sink.get())
        {
            setup_error_ = "same path returned different LogFileSink instances";
        }
    }

    void recordSetupError(const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if(setup_error_.empty())
        {
            setup_error_ = error;
        }
    }

    FileAppender::Ptr sharedAppender(const LogFileSink::Ptr& sink)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if(shared_appender_)
        {
            return shared_appender_;
        }

        shared_appender_ = std::make_shared<FileAppender>(sink);
        shared_appender_->setLevel(LogLevel::DEBUG);
        shared_appender_->setFormatter(
            std::make_shared<LogFormatter>("%m%n"));

        std::string open_error;
        if(!shared_appender_->openForAppend(&open_error))
        {
            setup_error_ = "cannot open shared FileAppender sink: "
                + open_error;
        }
        return shared_appender_;
    }

    void flushAfterAllWrites(int thread_index,
        const FileAppender::Ptr& appender)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        ++writes_finished_;
        if(writes_finished_ == thread_count_)
        {
            cv_.notify_all();
        }
        cv_.wait(lock, [this] {
            return writes_finished_ == thread_count_;
        });
        lock.unlock();

        if(0 == thread_index)
        {
            appender->flush();
        }

        lock.lock();
        if(0 == thread_index)
        {
            flushed_ = true;
            cv_.notify_all();
            return;
        }
        cv_.wait(lock, [this] { return flushed_; });
    }

    void finish(int thread_index, size_t records)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        records_per_thread_.at(static_cast<size_t>(thread_index)) = records;
        ++finished_;
        if(finished_ == thread_count_)
        {
            writeManifestUnlocked();
            run_active_ = false;
            cv_.notify_all();
            return;
        }
        cv_.wait(lock, [this] { return !run_active_; });
    }

    std::string setupError() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return setup_error_;
    }

private:
    void writeManifestUnlocked()
    {
        std::ofstream manifest(
            manifest_path_, std::ios::out | std::ios::trunc);
        if(!manifest.is_open())
        {
            setup_error_ = "cannot create validation manifest: "
                + manifest_path_;
            return;
        }

        manifest << "version=1\n"
                 << "variant=" << variant_ << '\n'
                 << "log_file=" << path_ << '\n'
                 << "thread_count=" << thread_count_ << '\n'
                 << "sink_shared=" << (shared_sink_ ? 1 : 0) << '\n'
                 << "setup_error=" << setup_error_ << '\n';
        for(size_t index = 0; index < records_per_thread_.size(); ++index)
        {
            manifest << "records_" << index << '='
                     << records_per_thread_[index] << '\n';
        }
        manifest.flush();
        if(!manifest.good())
        {
            setup_error_ = "cannot write validation manifest: "
                + manifest_path_;
        }
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool run_active_{false};
    int thread_count_{0};
    int arrived_{0};
    int ready_{0};
    int writes_finished_{0};
    bool flushed_{false};
    int finished_{0};
    uint64_t run_number_{0};
    std::string variant_;
    std::string path_;
    std::string manifest_path_;
    std::vector<size_t> records_per_thread_;
    std::string setup_error_;
    LogFileSink* shared_sink_{nullptr};
    FileAppender::Ptr shared_appender_;
};

FileAppenderBenchmarkRun& GetFileAppenderBenchmarkRun()
{
    static FileAppenderBenchmarkRun run;
    return run;
}

/*
测试思路：
1. 使用 DEBUG 级别调用，而 logger 的有效级别设置为 ERROR；
2. 验证日志在 shouldLog() 阶段被过滤；
3. 由于过滤发生在宏创建 LogAttr 之前，本用例不应执行 formatter、appender 或文件 I/O。

示例：
KIT_DEBUG(KIT_LOGGER("bench.filtered"), "benchmark")
    << "filtered message";

测量范围：
KIT_LOGGER()、Logger::shouldLog()、logger map 锁和 shared_ptr 拷贝。

排除范围：
LogAttr 构造、stringstream 写入、formatter、appender、文件 I/O。
*/
void BM_LevelFiltered(benchmark::State& state)
{
    (void)GetFilteredLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(KIT_LOGGER(kFilteredLoggerName), "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LevelFiltered);
/*
perf stat 固定迭代用例：
两个过滤路径都执行相同的一亿次调用，避免 Google Benchmark
根据运行速度自动选择不同迭代次数，使 cycles/op 和 instructions/op 可比较。
*/
BENCHMARK(BM_LevelFiltered)
    ->Name("BM_LevelFilteredPerfStat")
    ->Iterations(kPerfStatIterations);

/*
测试思路：
1. 复用已经初始化好的 Logger::Ptr；
2. 每轮仍然调用 KIT_DEBUG 和 Logger::shouldLog；
3. 与 BM_LevelFiltered 对比，隔离 LogManager::getLogger()
   以及 loggers_mtx_ 的成本。

示例：
auto logger = GetFilteredLogger();
KIT_DEBUG(logger, "benchmark")
    << "this message must be filtered";

测量范围：
shared_ptr 拷贝、KIT_DEBUG 宏、Logger::shouldLog()
和 appenders_mtx_。

排除范围：
LogManager::getLogger()、logger map 查找、
loggers_mtx_、LogAttr、formatter 和 I/O。
*/
void BM_LevelFilteredCached(benchmark::State& state)
{
    auto logger = GetFilteredLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(logger, "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_LevelFilteredCached);
BENCHMARK(BM_LevelFilteredCached)
    ->Name("BM_LevelFilteredCachedPerfStat")
    ->Iterations(kPerfStatIterations);

/*
测试思路：保留 LogManager 查找、hash、map 锁和 shouldLog，
排除 const char* 到临时 std::string 的重复构造。
*/
void BM_LevelFilteredPreparedName(benchmark::State& state)
{
    (void)GetFilteredLogger();
    const std::string logger_name{kFilteredLoggerName};

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(KIT_LOGGER(logger_name), "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LevelFilteredPreparedName);
BENCHMARK(BM_LevelFilteredPreparedName)
    ->Name("BM_LevelFilteredPreparedNamePerfStat")
    ->Iterations(kPerfStatIterations);

/*
测试思路：保留 LogManager 查找、hash、map 锁和 shouldLog，保留超过SSO的 std::string 的重复构造
*/
void BM_LevelFilteredLongName(benchmark::State& state)
{
    (void)GetFilteredLongNameLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(KIT_LOGGER(kFilteredLongLoggerName), "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LevelFilteredLongName);
BENCHMARK(BM_LevelFilteredLongName)
    ->Name("BM_LevelFilteredLongNamePerfStat")
    ->Iterations(kPerfStatIterations);

/*
测试思路：保留 LogManager 查找、hash、map 锁和 shouldLog
排除 const char* 到临时超过SSO的 std::string 的重复构造。
*/
void BM_LevelFilteredLongNamePrepared(benchmark::State& state)
{
    (void)GetFilteredLongNameLogger();
    const std::string logger_name{kFilteredLongLoggerName};

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(KIT_LOGGER(logger_name), "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LevelFilteredLongNamePrepared);
BENCHMARK(BM_LevelFilteredLongNamePrepared)
    ->Name("BM_LevelFilteredLongNamePreparedPerfStat")
    ->Iterations(kPerfStatIterations);

/*
测试思路：
直接重复调用 LogManager::getLogger("base")，
测量旧实现中的 loggers_mtx_、unordered_map 查找和 shared_ptr 拷贝。

示例：
每轮获取一次已经存在的 base Logger。

包含：
LogManager 单例访问、mutex、hash/map 查找、shared_ptr 拷贝。

排除：
shouldLog、LogAttr、formatter、appender 和 I/O。
*/
void BM_ModuleLoggerAccessLegacy(benchmark::State& state)
{
    (void)LogManager::GetInstance().getLogger(kModuleLoggerName);;

    for(auto _ : state)
    {
        (void)_;

        auto logger = LogManager::GetInstance().getLogger(kModuleLoggerName);

        benchmark::DoNotOptimize(logger);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModuleLoggerAccessLegacy)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLoggerAccessLegacy)
    ->Name("BM_ModuleLoggerAccessLegacyPerfStat")
    ->Iterations(kPerfStatIterations);

/*
测试思路：
重复调用 GetLoggerHelper("base") 缓存接口
测量新实现中的模块名分支、函数内 static 访问和 shared_ptr 拷贝。

示例：
每轮获取一次已经存在的 base Logger，第一次走旧路径, 后续都使用缓存的static实体

包含：
GetLoggerHelper 的模块分支判断、函数内 static LoggerPtr 读取和 shared_ptr 拷贝。

排除：
首次 static 初始化、LogManager、mutex、unordered_map、hash、formatter、appender 和 I/O。
*/
void BM_ModuleLoggerAccessCached(
    benchmark::State& state)
{
    (void)log_detail::GetLoggerHelper(kModuleLoggerName);

    for(auto _ : state)
    {
        (void)_;

        auto logger =
            log_detail::GetLoggerHelper(
                kModuleLoggerName);

        benchmark::DoNotOptimize(logger);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModuleLoggerAccessCached)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLoggerAccessCached)
    ->Name("BM_ModuleLoggerAccessCachedPerfStat")
    ->Iterations(kPerfStatIterations);


/*
测试思路：
1. 走旧设计旧 lookup + shouldLog() 使用 DEBUG 级别调用，而 logger 的有效级别设置为 ERROR；
2. 验证日志在 shouldLog() 阶段被过滤；
3. 由于过滤发生在宏创建 LogAttr 之前，本用例不应执行 formatter、appender 或文件 I/O。


测量范围：
KIT_LOGGER()、Logger::shouldLog()、logger map 锁和 shared_ptr 拷贝。

排除范围：
LogAttr 构造、stringstream 写入、formatter、appender、文件 I/O。
*/
void BM_ModuleLevelFilteredLegacy(
    benchmark::State& state)
{
    (void)PrepareFilteredModuleLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(
            LogManager::GetInstance().getLogger(
                kModuleLoggerName),
            "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModuleLevelFilteredLegacy)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLevelFilteredLegacy)
    ->Name("BM_ModuleLevelFilteredLegacyPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kPerfStatIterationsPerThread)
    ->UseRealTime();


/*
测试思路：
1. 走新设计旧 cached lookup + shouldLog() 使用 DEBUG 级别调用，而 logger 的有效级别设置为 ERROR；
2. 验证日志在 shouldLog() 阶段被过滤；
3. 由于过滤发生在宏创建 LogAttr 之前，本用例不应执行 formatter、appender 或文件 I/O。


测量范围：
KIT_LOGGER()、Logger::shouldLog()、logger map 锁和 shared_ptr 拷贝。

排除范围：
LogAttr 构造、stringstream 写入、formatter、appender、文件 I/O。
*/
void BM_ModuleLevelFilteredCached(
    benchmark::State& state)
{
    log_detail::GetLoggerHelper(kModuleLoggerName)->setLevel(LogLevel::ERROR);

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(
            log_detail::GetLoggerHelper(
                kModuleLoggerName),
            "benchmark")
            << "this message must be filtered";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModuleLevelFilteredCached)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLevelFilteredCached)
    ->Name("BM_ModuleLevelFilteredCachedPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kPerfStatIterationsPerThread)
    ->UseRealTime();

/*
完整 CPU 日志路径(单个appdder 不输出)：
1. Logger 和 DiscardAppender 都允许 DEBUG 日志，确保日志不会在 shouldLog()
   阶段结束；
2. 每轮通过 KIT_DEBUG 构造 LogAttr，经过 LogAttrWrap、logUnchecked、Appender
   快照和 LogAppender::append()；
3. DiscardAppender 不做 formatter 或 I/O，只保留日志 CPU 路径和锁成本。

当前基线固定使用缓存 Logger 获取路径和 shouldLog() 的原子快速路径；
旧 lookup 与旧锁路径由上面的独立 benchmark 用例保留作历史对照。
*/
void BM_ModuleLogDiscard(benchmark::State& state)
{
    (void)GetDiscardLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(
            KIT_LOGGER(kModuleLoggerName),
            "benchmark")
            << "discarded full-path message";
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModuleLogDiscard)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLogDiscard)
    ->Name("BM_ModuleLogDiscardPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();



void BM_ModuleLogConsoleE2E(benchmark::State& state)
{
    (void)GetSimulatConsoleLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(
            KIT_LOGGER(kModuleLoggerName),
            "benchmark")
            << kConsoleMessage;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * kConsoleMessageBytes);
}
BENCHMARK(BM_ModuleLogConsoleE2E)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLogConsoleE2E)
    ->Name("BM_ModuleLogConsoleE2EPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


void BM_ModuleLogConsole(benchmark::State& state)
{
    (void)GetConsoleLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(
            KIT_LOGGER(kModuleLoggerName),
            "benchmark")
            << kConsoleMessage;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * kConsoleMessageBytes);
}
BENCHMARK(BM_ModuleLogConsole)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLogConsole)
    ->Name("BM_ModuleLogConsolePerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


void BM_ModuleLogFile(benchmark::State& state)
{
    (void)GetFileLogger();

    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(
            KIT_LOGGER(kModuleLoggerName),
            "benchmark")
            << kConsoleMessage;
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * kConsoleMessageBytes);
}
BENCHMARK(BM_ModuleLogFile)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_ModuleLogFile)
    ->Name("BM_ModuleLogFilePerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


enum class FileAppenderTopology
{
kSingleSharedAppender,
kMultipleAppenders,
};

void RunFileAppenderSamePath(benchmark::State& state,
    FileAppenderTopology topology)
{
    const bool multiple =
        FileAppenderTopology::kMultipleAppenders == topology;
    const std::string variant = multiple
        ? "multiple-file-appenders-same-path"
        : "single-file-appender-same-path";
    auto& run = GetFileAppenderBenchmarkRun();

    const std::string path = run.begin(state.threads(), variant);
    auto sink = LogManager::GetInstance().acquireFileSink(path);
    run.registerSink(sink);

    FileAppender::Ptr appender;
    if(multiple)
    {
        appender = std::make_shared<FileAppender>(sink);
        appender->setLevel(LogLevel::DEBUG);
        appender->setFormatter(std::make_shared<LogFormatter>("%m%n"));

        std::string open_error;
        if(!appender->openForAppend(&open_error))
        {
            run.recordSetupError(
                "cannot open multiple FileAppender sink: " + open_error);
        }
    }
    else
    {
        appender = run.sharedAppender(sink);
    }

    auto logger = std::make_shared<Logger>(
        std::string("benchmark.file-appender.")
        + std::to_string(state.thread_index()));
    logger->setLevel(LogLevel::DEBUG);
    logger->addAppender(appender);
    run.waitReady();

    const std::string setup_error = run.setupError();
    if(!setup_error.empty())
    {
        run.finish(state.thread_index(), 0);
        state.SkipWithError(setup_error.c_str());
        return;
    }
    size_t sequence = 0;
    for(auto _ : state)
    {
        (void)_;

        KIT_DEBUG(logger, "benchmark")
            << "mfa thread=" << state.thread_index()
            << " seq=" << sequence++;
    }

    run.flushAfterAllWrites(state.thread_index(), appender);
    run.finish(state.thread_index(), sequence);

    const std::string final_error = run.setupError();
    if(!final_error.empty())
    {
        state.SkipWithError(final_error.c_str());
    }
    state.SetItemsProcessed(sequence);
}

/*
测试思路：
1. 每个线程使用独立 Logger 和独立 FileAppender；
2. 所有 FileAppender 经 registry 获取同一路径的同一个 LogFileSink；
3. 每轮写入带 thread/sequence 的完整记录；
4. benchmark 进程只生成日志和预期记录 manifest，文件解析由外部脚本完成。

并发图：
thread 0 -> Logger 0 -> FileAppender 0 -+
thread 1 -> Logger 1 -> FileAppender 1 -+-> shared LogFileSink -> one file
thread N -> Logger N -> FileAppender N -+

示例：线程 3 的第 17 条记录为 `mfa thread=3 seq=17`。
*/
void BM_MultipleFileAppendersSamePath(benchmark::State& state)
{
    RunFileAppenderSamePath(
        state, FileAppenderTopology::kMultipleAppenders);
}
BENCHMARK(BM_MultipleFileAppendersSamePath)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_MultipleFileAppendersSamePath)
    ->Name("BM_MultipleFileAppendersSamePathPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


/*
测试思路：
1. 每个线程仍使用独立 Logger，保持 Logger 查找和派发方式与多 Appender 用例一致；
2. 所有 Logger 持有同一个 FileAppender；
3. formatter、记录内容、LogFileSink、线程数和固定工作量与多 Appender 用例一致；
4. 与 BM_MultipleFileAppendersSamePath 配对，隔离一个共享 FileAppender 和多个
   独立 FileAppender 的成本差异。

并发图：
thread 0 -> Logger 0 -+
thread 1 -> Logger 1 -+-> one shared FileAppender -> shared LogFileSink -> one file
thread N -> Logger N -+

示例：线程 3 的第 17 条记录同样为 `mfa thread=3 seq=17`。
*/
void BM_SingleFileAppenderSamePath(benchmark::State& state)
{
    RunFileAppenderSamePath(
        state, FileAppenderTopology::kSingleSharedAppender);
}
BENCHMARK(BM_SingleFileAppenderSamePath)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_SingleFileAppenderSamePath)
    ->Name("BM_SingleFileAppenderSamePathPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


/*
测试思路：
1. 所有 benchmark 线程直接调用同一个 LogFileSink::append()；
2. 排除 Logger、LogAttr、formatter 和 Appender，只保留共享 sink 锁、ofstream
   写入、current size 维护以及阈值 flush；
3. 与 BM_LogFileSinkAppendIndependent 对比，量化同一路径串行写入的竞争成本。

并发图：thread 0..N -> shared LogFileSink::mtx -> one ofstream。
示例：每轮向同一个 /tmp 文件追加固定 25 字节 payload。
*/
void BM_LogFileSinkAppendShared(benchmark::State& state)
{
    auto sink = GetSharedDirectFileSink();

    for(auto _ : state)
    {
        (void)_;
        sink->append(kConsoleMessage);
    }

    sink->flush();
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * kConsoleMessageBytes);
}
BENCHMARK(BM_LogFileSinkAppendShared)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_LogFileSinkAppendShared)
    ->Name("BM_LogFileSinkAppendSharedPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


/*
测试思路：
1. 每个 benchmark 线程直接写入自己的 LogFileSink 和文件路径；
2. 每个 sink 仍执行与 Shared 用例相同的 append、大小维护和阈值 flush；
3. 不同路径不共享 sink mutex，因此结果表示无关文件之间的并行写入基线。

并发图：thread i -> sink[i]::mtx -> ofstream[i]。
示例：8 个线程分别写 direct-file-sink-independent-<id>-0..7.log。
*/
void BM_LogFileSinkAppendIndependent(benchmark::State& state)
{
    auto sink = GetIndependentDirectFileSink(state.thread_index());

    for(auto _ : state)
    {
        (void)_;
        sink->append(kConsoleMessage);
    }

    sink->flush();
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * kConsoleMessageBytes);
}
BENCHMARK(BM_LogFileSinkAppendIndependent)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(BM_LogFileSinkAppendIndependent)
    ->Name("BM_LogFileSinkAppendIndependentPerfStat8")
    ->Threads(kPerfStatThreads)
    ->Iterations(kFullPathPerfStatIterationsPerThread)
    ->UseRealTime();


// main
int main(int argc, char** argv)
{
    // 日志系统部署在多线程服务器中。预先创建并回收一个线程，
    // 避免 libstdc++ 的 shared_ptr 单线程快路径导致结果依赖 benchmark 执行顺序。
    std::thread([] {}).join();

    benchmark::Initialize(&argc, argv);
    if(benchmark::ReportUnrecognizedArguments(argc, argv))
    {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
