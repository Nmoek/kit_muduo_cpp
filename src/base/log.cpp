/**
 * @file log.cpp
 * @brief 日志器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 19:32:10
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log.h"
#include "base/log_appender.h"
#include "base/log_config.h"
#include "base/log_file_sink.h"
#include "base/log_formatter.h"
#include "base/log_attr.h"
#include "base/log_inner.h"

#include <atomic>
#include <cassert>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace kit_muduo {


struct PreparedLogConfig
{
    LogFileConfig file_config;
    LogFileSinkRegister::SinksMap sinks;
    std::unordered_map<std::string, Logger::Ptr> loggers;
};


namespace {


LogFormatter::Ptr MakeFormatter(const std::string &pattern)
{
    auto formatter = std::make_shared<LogFormatter>(pattern.empty() ? kLogFormatDefaultPattern : pattern);
    if(!formatter->valid())
    {
        throw std::invalid_argument(
            "invalid log formatter: " + formatter->error());
    }
    return formatter;
}

LogFileSink::Ptr FindAndMakeSink(LogFileSinkRegister& file_register, 
    LogFileSinkRegister::SinksMap &sinks,
    const std::string &normalize_path)
{
    LogFileSink::Ptr sink = nullptr;
    
    auto it = sinks.find(normalize_path);
    if(it != sinks.end()) 
    {
        if((sink = it->second.lock()) != nullptr)
        {
            return sink;
        }
    }

    sink = std::make_shared<LogFileSink>(&file_register, normalize_path);

    auto open_result = sink->reopen();
    if(!open_result.ok())
    {
        LOG_INNER_ERROR("log file sink reopen error [%s]: %s \n", normalize_path.c_str(), open_result.message.c_str());
        return nullptr;
    }
    sinks[normalize_path] = sink;

    return sink;
}

// 注意 这里面不显式抛异常
LogAppender::Ptr MakeAppender(LogFileSinkRegister& file_register,
    LogFileSinkRegister::SinksMap& sinks,
    const LogAppenderConfig &config, 
    const LogFormatter::Ptr &formatter, 
    const std::string& logger_name,
    size_t appender_index)
{
    LogAppender::Ptr appender;
    if(LogAppenderType::kStdout == config.type)
    {
        auto c = std::make_shared<ConsoleAppender>();
        c->setLevel(config.level);
        c->setFormatter(config.formatter.empty() ? formatter : MakeFormatter(config.formatter));
        appender = std::move(c);
    }
    else
    {
        const std::string normalize_path = NormalizeFilePath(config.file_path);

        LogFileSink::Ptr sink = FindAndMakeSink(file_register, sinks, normalize_path);
        if(!sink)
        {
            LOG_INNER_ERROR("system.log.loggers[%s].appenders[%ld].file_path= %s\n", logger_name.c_str(), appender_index, config.file_path.c_str());

            return nullptr;
        }

        auto f = std::make_shared<FileAppender>(sink);
        f->setLevel(config.level);
        f->setFormatter(config.formatter.empty() ? formatter : MakeFormatter(config.formatter));

        appender = std::move(f);
    }

    return appender;
}


} // namespace

class LogSubmissionGuard
{
public:
    LogSubmissionGuard(LogManager &manager)
        :manager_(manager)
        ,entered_(manager_.tryEnterLogSubmission())
    { }

    ~LogSubmissionGuard()
    {
        if(entered_)
        {
            manager_.leaveLogSubmission();
        }
    }

    bool entered() const noexcept
    {
        return entered_;
    }

private:
    LogManager &manager_;
    bool entered_{false};
};

Logger::Logger(LogManager* manager, const std::string &name)
    :manager_(manager)
    ,name_(name)
    ,level_(LogLevel::DEBUG)
{
    assert(manager_);
}

/*
             |  _level(INFO) <= level都能进行输出
             v
    DEBUG   INFO   WARN  ERROR   FATAL
            INFO
*/
void Logger::log(LogAttr::Ptr attr)
{
    if(!attr || !shouldLog(attr->getLevel()))
    {
        return;
    }

    if (!attr->isSealed())
    {
        LOG_INNER_ERROR("log attr not sealed! \n");
        attr->seal();
    }

    logUnchecked(attr);
}


void Logger::logUnchecked(LogAttr::Ptr attr)
{
    if(!attr)
    {
        return;
    }

    // 日志提交RAII
    LogSubmissionGuard guard(*manager_);

    if(!guard.entered())
    {
        LOG_INNER_DEBUG("log attr submission rejected! \n");
        return;
    }

    dispatchUnlocked(attr);
}

void Logger::dispatchUnlocked(LogAttr::Ptr attr)
{
    if(auto root = getRootFallback())
    {
        root->dispatchUnlocked(std::move(attr));
        return;
    }
    std::vector<LogAppender::Ptr> appender_snapshot;

    std::unique_lock<std::mutex> lock(appenders_mtx_);
    appender_snapshot.assign(appenders_.begin(), appenders_.end());
    lock.unlock();

    // 快照持有 shared_ptr，保证本轮派发完成前 Appender 保持有效。
    for(auto &a : appender_snapshot)
    {
        if(a)
        {
            a->append(attr);
        }
    }
}

void Logger::addAppender(LogAppender::Ptr pappender)
{
    std::unique_lock<std::mutex> lock(appenders_mtx_);
    root_fallback_.reset();
    appenders_.push_back(std::move(pappender));
    output_route_.store(OutputRoute::kOwnAppenders, std::memory_order_release);
}

void Logger::delAppender(LogAppender::Ptr pappender)
{
    std::unique_lock<std::mutex> lock(appenders_mtx_);

    for(auto it = appenders_.begin();it != appenders_
    .end();++it)
    {
        if(*it == pappender)
        {
            appenders_.erase(it);
            if(appenders_.empty())
            {
                output_route_.store(OutputRoute::kMuted, std::memory_order_release);
            }
            return;
        }
    }
    return;
}

void Logger::clearAppender()
{
    std::unique_lock<std::mutex> lock(appenders_mtx_);
    appenders_.clear();
    output_route_.store(OutputRoute::kMuted, std::memory_order_release);
}

bool Logger::shouldLog(LogLevel::Level level) const
{
    auto route = output_route_.load(std::memory_order_acquire);

    // 静音状态不输出
    if(OutputRoute::kMuted == route)
    {
        return false;
    }
    // 慢路径单独拆分
    if(OutputRoute::kRootFallback == route)
    {
        return shouldLogWithRootFallback(level);
    }

    return level >= getLevel();
}

void Logger::replaceAppenders(std::list<LogAppender::Ptr> appenders)
{
    const auto route = appenders.empty()
        ? OutputRoute::kMuted
        : OutputRoute::kOwnAppenders;

    std::lock_guard<std::mutex> lock(appenders_mtx_);
    root_fallback_.reset();
    appenders_ = std::move(appenders);

    output_route_.store(route, std::memory_order_release);
}

void Logger::useRootFallback(const Logger::Ptr& root)
{
    if(!root || root.get() == this)
    {
        throw std::logic_error("root fallback requires another logger");
    }

    std::lock_guard<std::mutex> lock(appenders_mtx_);
    appenders_.clear();
    root_fallback_ = root;

    output_route_.store(OutputRoute::kRootFallback, std::memory_order_release);
}

Logger::Ptr Logger::getRootFallback() const
{
    if(OutputRoute::kRootFallback != output_route_.load())
    {
        return nullptr;
    }

    // double check
    std::lock_guard<std::mutex> lock(appenders_mtx_);

    if(OutputRoute::kRootFallback != output_route_.load())
    {
        return nullptr;
    }

    return root_fallback_.lock();
}

bool Logger::shouldLogWithRootFallback(LogLevel::Level level) const
{

    // double check
    std::unique_lock<std::mutex> lock(appenders_mtx_);
    const auto route = output_route_.load(std::memory_order_acquire);
    
    if(OutputRoute::kMuted == route)
    {
        return false;
    }

    if(OutputRoute::kOwnAppenders == route)
    {
        return level >= getLevel();
    }

    auto root = root_fallback_.lock();
    
    assert(root.get() != this);  // 不能自己指向自己fallback
    lock.unlock(); // 一定要解锁

    return root && root->shouldLog(level);
    
}

/**************LogAttrWrap****************/

LogAttrWrap::LogAttrWrap(LogAttr::Ptr attr)
    :attr_(std::move(attr))
{ }

LogAttrWrap::~LogAttrWrap() noexcept
{
    try {

        if(attr_ && attr_->getLogger())
        {
            attr_->seal();
            attr_->getLogger()->logUnchecked(attr_);
        }

    } catch(const std::exception &e) {
        LOG_INNER_EXCPTION("log submit exception: %s\n", e.what());
    } catch(...) {
        LOG_INNER_EXCPTION("log submit unknown exception\n");
    }
}



/**************LogManager****************/


LogManager& LogManager::GetInstance()
{
    static LogManager m;
    return m;
}

LogManager::LogManager()
    :state_(LogManagerState::kInit)
    ,root_logger_(std::make_shared<Logger>(this, "root"))
{
    loggers_.emplace("root", root_logger_);

}

LogManagerResult LogManager::initialize(const LogConfig& config)
{
    LogManagerState expected = LogManagerState::kInit;
    // 必须是 kInit-->kPreparing
    if(!state_.compare_exchange_strong(expected, LogManagerState::kPreparing, std::memory_order_acq_rel))
    {
        if(LogManagerState::kRunning == expected)
        {
            return LogManagerResult::Failure(LogManagerResultStatus::kAlreadyInitialized, 
                "log manager already initialized");
        }
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager is not in init state");
    }

    try {

        applyConfig(config);

        state_.store(LogManagerState::kRunning, std::memory_order_release);

        return LogManagerResult::Ok();

    } catch(const std::exception &e) {
        state_.store(LogManagerState::kInit, std::memory_order_release);

        return LogManagerResult::Failure(LogManagerResultStatus::kPrepareFailed, std::string("prepared excption: ") + e.what());
    } catch(...) {
        state_.store(LogManagerState::kInit, std::memory_order_release);

        return LogManagerResult::Failure(LogManagerResultStatus::kPrepareFailed, "prepared unknown excption");
    }
}

Logger::Ptr LogManager::getRootLogger() const
{
    return root_logger_;
}

void LogManager::addLogger(const std::string &name, Logger::Ptr logger)
{
    std::unique_lock<std::mutex> lock(loggers_mtx_);
    loggers_[name] = logger;
}

Logger::Ptr LogManager::addLogger(const std::string &name)
{
    std::unique_lock<std::mutex> lock(loggers_mtx_);
    auto logger = std::make_shared<Logger>(this, name);
    logger->useRootFallback(root_logger_);
    loggers_[name] = logger;
    return logger;
}



Logger::Ptr LogManager::getLogger(const std::string& name)
{
    std::lock_guard<std::mutex> lock(loggers_mtx_);
    return findOrCreateUnLocked(name);
}


Logger::Ptr LogManager::findOrCreateUnLocked(const std::string& name)
{
    if("root" == name)
    {
        return root_logger_;
    }

    auto it = loggers_.find(name);
    if(it != loggers_.end())
    {
        return it->second;
    }

    auto logger = std::make_shared<Logger>(this, name);
    logger->useRootFallback(root_logger_);
    loggers_.emplace(name, logger);
    return logger;
}


// TODO 增加inotify监听文件变化 热更新时配置文件发生变化才需要加载  

// HACK 这里保留接口是为了兼容灵活测试
void LogManager::applyConfig(const LogConfig &config)
{
    ValidateLogConfig(config);

    // BUG 这里设计始终有点问题 半热更新设计

    auto prepared = prepareLogConfig(config);

    publishPreparedConfig(std::move(prepared));
}

LogFileSink::Ptr LogManager::acquireFileSink(const std::string &file_path)
{
    return file_register_.acquire(file_path);
}

LogManagerResult LogManager::flushAll()
{
    const auto current = state();

    if (LogManagerState::kRunning != current
        && LogManagerState::kStopAccepting != current)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager cannot flush in current state");
    }

    const auto result = file_register_.flushAll();

    if (!result.ok())
    {
        return LogManagerResult::Failure(
            LogManagerResultStatus::kOperationFailed,
            result.message);
    }

    return LogManagerResult::Ok();
}

LogManagerResult LogManager::flush(const std::string& file_path)
{
    const auto current = state();

    if (LogManagerState::kRunning != current
        && LogManagerState::kStopAccepting != current)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager cannot flush in current state");
    }

    const auto result = file_register_.flush(file_path);
    if (!result.ok())
    {
        return LogManagerResult::Failure(
            LogManagerResultStatus::kOperationFailed,
            result.message);
    }

    return LogManagerResult::Ok();
}

LogManagerResult LogManager::durableFlushAll()
{
    const auto current = state();

    if (LogManagerState::kRunning != current
        && LogManagerState::kStopAccepting != current)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager cannot durable flush in current state");
    }

    const auto result = file_register_.durableFlushAll();

    if (!result.ok())
    {
        return LogManagerResult::Failure(
            LogManagerResultStatus::kOperationFailed,
            result.message);
    }

    return LogManagerResult::Ok();
}

LogManagerResult LogManager::durableFlush(const std::string& file_path)
{
    const auto current = state();

    if (LogManagerState::kRunning != current
        && LogManagerState::kStopAccepting != current)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager cannot durable flush in current state");
    }

    const auto result = file_register_.durableFlush(file_path);

    if (!result.ok())
    {
        return LogManagerResult::Failure(
            LogManagerResultStatus::kOperationFailed,
            result.message);
    }

    return LogManagerResult::Ok();
}

LogManagerResult LogManager::reopenAll()
{
    const auto current = state();

    if (LogManagerState::kRunning != current
        && LogManagerState::kStopAccepting != current)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager cannot repoen in current state");
    }
    
    const auto result = file_register_.reopenAll();
    if (!result.ok())
    {
        return LogManagerResult::Failure(
            LogManagerResultStatus::kOperationFailed,
            result.message);
    }

    return LogManagerResult::Ok();
}

LogManagerResult LogManager::reopen(const std::string& file_path)
{
    const auto current = state();

    if (LogManagerState::kRunning != current
        && LogManagerState::kStopAccepting != current)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState,
            "log manager cannot repoen in current state");
    }
    
    const auto result = file_register_.reopen(file_path);
    if (!result.ok())
    {
        return LogManagerResult::Failure(
            LogManagerResultStatus::kOperationFailed,
            result.message);
    }

    return LogManagerResult::Ok();
}

LogManagerResult LogManager::shutdown()
{
    std::unique_lock<std::mutex> lock(lifecycle_mtx_);

    const auto cur_state = state();

    if(LogManagerState::kStopped == cur_state)
    {
        return LogManagerResult::Ok();
    }

    if(LogManagerState::kRunning != cur_state)
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kInvalidState, "log manager cannot shutdown in current state");
    }

    // 先阻止新的日志提交。
    accepting_logs_ = false;

    state_.store(LogManagerState::kStopAccepting, std::memory_order_release);


    // 等待内存中日志完全写入 drain
    lifecycle_cv_.wait(lock, [this](){
        return 0 == active_log_submissions_;
    });

    // 从这里开始 所有日志不再进入系统
    const auto flush_result = file_register_.flushAll();

    file_register_.closeAll();

    state_.store(LogManagerState::kStopped, std::memory_order_release);

    if(!flush_result.ok())
    {
        return LogManagerResult::Failure(LogManagerResultStatus::kOperationFailed, "log manager flushAll error: " + flush_result.message);
    }
    return LogManagerResult::Ok();
}

LogManagerHealthSnapshot
LogManager::healthSnapshot() const
{
    LogManagerHealthSnapshot result;
    result.manager_state = state();
    const auto sinks = file_register_.snapshotSinks();

    result.sinks.reserve(sinks.size());

    for (const auto& sink : sinks)
    {
        if (!sink)
        {
            continue;
        }

        auto health = sink->healthSnapshot();

        result.written_records += health.written_records;

        result.written_bytes += health.written_bytes;

        result.write_failures += health.write_failures;

        result.flush_failures += health.flush_failures;

        result.reopen_failures += health.reopen_failures;

        result.rotate_failures += health.rotate_failures;

        result.fallback_records += health.fallback_records;

        result.truncated_records += health.truncated_records;

        result.sinks.push_back(LogManagerHealthSnapshot::SinkEntry{
            .path = sink->normalizedPath(),
            .health = std::move(health)
        });
    }

    return result;
}

PreparedLogConfig LogManager::prepareLogConfig(const LogConfig& config)
{
    PreparedLogConfig prepared;
    prepared.file_config = std::move(config.file);

    // 文件注册使用副本操作，有的已打开的文件不需要重新打开
    prepared.sinks = file_register_.logFileSinks();

    std::list<LogAppender::Ptr> appenders;


    for(const auto& logger_config : config.loggers)
    {

        appenders.clear();
        auto formatter = MakeFormatter(logger_config.formatter);

        size_t appender_index = 0;
        for(const auto& appender_config : logger_config.appenders)
        {
            auto appender = MakeAppender(file_register_,
                prepared.sinks,
                appender_config,
                formatter,
                logger_config.name,
                appender_index++);
            if(!appender)
            {
                continue;
            }

            // 设置日志最大输出上限
            appender->setMaxRecordBytes(config.max_record_bytes);
            appenders.push_back(appender);
        }

        auto logger = std::make_shared<Logger>(this, logger_config.name);
        logger->setLevel(logger_config.level);
        logger->replaceAppenders(std::move(appenders));

        // 通过配置新创建的一批logger日志器
        auto [it, is_inserted] = prepared.loggers.emplace(logger_config.name, std::move(logger));

        if(!is_inserted)
        {
            throw std::runtime_error("duplicate prepared logger: " + logger_config.name);
        }
    }

    // 配置中必须包含root
    if(prepared.loggers.find("root") == prepared.loggers.end())
    {
        throw std::runtime_error("logger config default 'root' not found");
    }

    return prepared;
}

void LogManager::publishPreparedConfig(PreparedLogConfig&& prepared)
{
    // TODO 观察者通知相关模块

    // 提交 预准备的文件持久化操作句柄
    file_register_.commit(std::move(prepared.sinks));
    
    // 提交 文件落盘相关配置
    file_register_.setFileConfig(std::move(prepared.file_config));

    // 发布 logger


    // 已存在logger配置更新
    std::lock_guard<std::mutex> lock(loggers_mtx_);

    auto it = prepared.loggers.find("root");
    assert(it != prepared.loggers.end());
    root_logger_ = std::move(it->second);
    loggers_["root"] = root_logger_;
    prepared.loggers.erase(it);

    for(auto &[name, logger] : loggers_)
    {
        if("root" == name)
        {
            continue;
        }
        it = prepared.loggers.find(name);
        if(it != prepared.loggers.end())
        {
            logger = std::move(it->second);
            prepared.loggers.erase(it);
        }
        else 
        {
            logger->useRootFallback(root_logger_);
        }
    }

    // 不存在logger新增
    loggers_.insert(prepared.loggers.begin(), prepared.loggers.end());

}

bool LogManager::tryEnterLogSubmission() noexcept
{
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if(!accepting_logs_ 
        || LogManagerState::kRunning != state_.load(std::memory_order_acquire))
    {
        return false;
    }
    ++active_log_submissions_;
    return true;
}

void LogManager::leaveLogSubmission() noexcept
{
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);

    if(active_log_submissions_ > 0)
    {
        --active_log_submissions_;
    }

    if(!accepting_logs_ && 0 == active_log_submissions_)
    {
        lifecycle_cv_.notify_all();
    }

}


namespace log_detail {

Logger::Ptr GetBaseLogger()
{
    static const auto logger = LogManager::GetInstance().getLogger("base");
    return logger;
}

Logger::Ptr GetNetLogger()
{
    static const auto logger = LogManager::GetInstance().getLogger("net");
    return logger;
}

Logger::Ptr GetWebLogger()
{
    static const auto logger = LogManager::GetInstance().getLogger("web");
    return logger;
}

Logger::Ptr GetDomainLogger()
{
    static const auto logger = LogManager::GetInstance().getLogger("domain");
    return logger;
}
} // namespace log_detail


} // namespace kit
