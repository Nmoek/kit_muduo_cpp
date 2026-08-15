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
#include <cstdint>
#include <thread>

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
        auto appender = std::make_shared<FileAppender>(std::string("/tmp/kit-log-bench/module-log-file-") + std::to_string(GetThreadPid()) + ".log");

        appender->setLevel(LogLevel::DEBUG);
        logger->addAppender(std::move(appender));
        return logger;
    }();

    return logger;
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

MUDUO_LOG_CACHE_MODULE_LOGGER=0/1 会同时覆盖每轮 Logger 获取路径；
MUDUO_LOG_SHOULDLOG_OPTIMIZE=0/1 会覆盖 shouldLog() 的旧锁/原子实现。
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
