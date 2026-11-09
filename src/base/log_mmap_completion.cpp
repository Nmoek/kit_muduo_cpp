/**
 * @file log_mmap_completion.cpp
 * @brief 日志mmap已封装批次双写操作管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-24 15:11:03
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_mmap_completion.h"
#include "base/log_mmap_batch.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

namespace kit_muduo {

namespace {

std::error_code CompressionStatusToError(CompressionResultStatus status) noexcept
{
    switch (status)
    {
    case CompressionResultStatus::kOk:
        return {};
    case CompressionResultStatus::kInvalidArgument:
        return std::make_error_code(std::errc::invalid_argument);
    case CompressionResultStatus::kInvalidState:
        return std::make_error_code(std::errc::operation_not_permitted);
    case CompressionResultStatus::kUnsupportedOption:
        return std::make_error_code(std::errc::not_supported);
    case CompressionResultStatus::kOutOfMemory:
        return std::make_error_code(std::errc::not_enough_memory);
    case CompressionResultStatus::kInitializationFailed:
    case CompressionResultStatus::kCompressionFailed:
    case CompressionResultStatus::kDecompressionFailed:
    case CompressionResultStatus::kIncompleteInput:
    case CompressionResultStatus::kCorruptedInput:
    case CompressionResultStatus::kOutputFailed:
    case CompressionResultStatus::kInternalError:
        return std::make_error_code(std::errc::io_error);
    }
    return std::make_error_code(std::errc::io_error);
}

std::error_code WriteAll(int fd, Span<const uint8_t> bytes) noexcept
{
    size_t offset = 0;
    while (offset < bytes.size())
    {
        const ssize_t n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n > 0)
        {
            offset += static_cast<size_t>(n); continue;
        }
        if (n < 0 && errno == EINTR)
        {
            continue;
        }

        return {n == 0 ? EIO : errno, std::generic_category()};
    }
    return {};
}


/**
 * @brief 尝试无覆盖修改文件名
 * @param from 
 * @param to 
 * @return std::error_code 
 */
std::error_code RenameMmapArchive(const std::string& from, const std::string& to) noexcept
{
#if defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) != 0) {
        return {errno, std::generic_category()};
    }
    return {};
#else
    std::error_code error;
    const bool is_exists = std::filesystem::exists(to, error);
    if(is_exists)
    {
        return std::make_error_code(std::errc::file_exists);
    }
    if(error)
    {
        return error;
    }
    error.clear();
    std::filesystem::rename(from, to, error);
    return error;
#endif
}

} // namespace

class ArchiveTempFile
{
public:
    explicit ArchiveTempFile(std::string path)
        :path_((std::move(path)))
    {
        fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd_ < 0)
        {
            throw std::system_error(errno, std::generic_category(), " log generation tmp file create error");
        }
    
    }
    ~ArchiveTempFile()
    {
        (void)close();

        // 实时压缩失败必然不能发布 直接删除tmp文件
        if(!published_)
        {
            remove();
        }
    }

    std::error_code append(Span<const uint8_t> bytes) noexcept
    {
        size_t offset = 0;
        while (offset < bytes.size())
        {
            const ssize_t n = ::write(fd_, bytes.data() + offset, bytes.size() - offset);
            if (n > 0)
            {
                offset += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            return {n == 0 ? EIO : errno, std::generic_category()};
        }
        return {};
    }
    std::error_code close() noexcept
    {
        if(fd_ < 0)
        {
            return {};
        }
        const int fd = std::exchange(fd_, -1);
        // Linux close(EINTR) 也不能再次 close 同一个数字，避免误关复用 fd。
        return ::close(fd) == 0 ? std::error_code{}
            : std::error_code(errno, std::generic_category());
    }

    std::error_code durableFlush() noexcept
    {
        for (;;) 
        {
            if(::fdatasync(fd_) < 0)
            {
                if (errno != EINTR)
                {
                    return {errno, std::generic_category()};
                }
                continue;
            }
            return {};
        } 
    }


    void markPublished() noexcept { published_ = true; }

    std::string path() const noexcept { return path_; }

private:
    void remove() noexcept
    {
        ::unlink(path_.c_str());
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

private:
    std::string path_;
    int32_t fd_{-1};
    bool published_{false};
};

LogMmapBatchCompletion::LogMmapBatchCompletion()
{

}

bool LogMmapBatchCompletion::startActive() noexcept
{
    return transition(active_, LogMmapBranchState::kPending, LogMmapBranchState::kRunning);
}

bool LogMmapBatchCompletion::completeActive(std::error_code ec, std::string message) noexcept
{
    return complete(active_, active_result_, ec, std::move(message));
}

bool LogMmapBatchCompletion::startCompression() noexcept
{
    return transition(compression_, LogMmapBranchState::kPending, LogMmapBranchState::kRunning);
}

bool LogMmapBatchCompletion::completeCompression(std::error_code ec, std::string message) noexcept
{
    return complete(compression_, compression_result_, ec, std::move(message));
}

bool LogMmapBatchCompletion::skipCompression(std::string reason) noexcept
{
    if(!transition(compression_, LogMmapBranchState::kPending, LogMmapBranchState::kRunning))
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(result_mutex_);

    compression_result_.error.clear();
    compression_result_.message = std::move(reason);

    compression_.store(LogMmapBranchState::kSkipped,std::memory_order_release);
    return true;
}
LogMmapBranchResult LogMmapBatchCompletion::activeResult() const
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    return active_result_;
}

LogMmapBranchResult LogMmapBatchCompletion::compressionResult() const
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    return compression_result_;
}

bool LogMmapBatchCompletion::bothTerminal() const noexcept
{
    return IsTerminal(activeState()) && IsTerminal(compressionState());
}

bool LogMmapBatchCompletion::canReleaseSlot() const noexcept
{
    return bothTerminal()
        && activeState() == LogMmapBranchState::kSucceeded;
}


bool LogMmapBatchCompletion::IsTerminal(LogMmapBranchState state) noexcept
{
    return state == LogMmapBranchState::kSucceeded
        || state == LogMmapBranchState::kFailed
        || state == LogMmapBranchState::kSkipped;
}

bool LogMmapBatchCompletion::complete(std::atomic<LogMmapBranchState>& state,
    LogMmapBranchResult& result,
    std::error_code error,
    std::string message) noexcept
{
    std::lock_guard<std::mutex> lock(result_mutex_);

    if(LogMmapBranchState::kRunning != state.load(std::memory_order_acquire))
    {
        return false;
    }

    result.error = error;
    result.message = std::move(message);
    state.store(error ? LogMmapBranchState::kFailed : LogMmapBranchState::kSucceeded);

    return true;
}

bool LogMmapBatchCompletion::transition(std::atomic<LogMmapBranchState>& state, LogMmapBranchState from, LogMmapBranchState to) noexcept
{
    return state.compare_exchange_strong(from, to, std::memory_order_acq_rel, std::memory_order_acquire);
}


/************MmapSealedBatch********** */

MmapSealedBatch::MmapSealedBatch(std::shared_ptr<MmapSlotLease> lease, 
    std::shared_ptr<MmapBatchTarget> target,
    bool compression_enabled)
    :lease_(std::move(lease))
    ,target_(std::move(target))
    ,compression_enabled_(compression_enabled)
    ,epoch_(lease_->slot().header().slot_epoch)
    ,generation_(lease_->slot().header().log_generation)
    ,sequence_(lease_->slot().header().log_batch_sequence)
    ,sealed_size_(lease_->slot().header().payload_size)
{

}

std::shared_ptr<MmapSealedBatch> MmapSealedBatch::Make(MmapSlot& slot,
    std::shared_ptr<MmapBatchTarget> target, bool compression_enabled, bool recovered)
{
    if(!target)
    {
        throw std::invalid_argument("mmap batch target null");
    }
    const auto &header  = slot.header();
    const auto state = static_cast<MmapSlotState>(header.state);

    const bool valid_state = MmapSlotState::kSealed == state || (recovered && MmapSlotState::kProcessing == state);
    if(!valid_state)
    {
        throw std::logic_error("invalid state for batch publication");
    }

    if(header.payload_size > slot.payloadCapacity())
    {
        throw std::logic_error("sealed payload exceeds slot capacity");
    }

    // 恢复流程要额外检查
    if(recovered)
    {
        if(0 == header.payload_size || 0 == header.log_generation || 0 == header.log_batch_sequence)
        {
            throw std::invalid_argument("invalid committed recovery batch");
        }
    }

    return std::make_shared<MmapSealedBatch>(std::make_shared<MmapSlotLease>(slot), std::move(target), compression_enabled);
}


/*****LogGenerationCompressionSession*****/

LogGenerationCompressionSession::LogGenerationCompressionSession(uint64_t generation, std::string path)
    :generation_(generation)
    ,output_(std::make_unique<ArchiveTempFile>(std::move(path)))
    ,compressor_(CompressionCodec::Create()->createCompressor())
{

}


CompressionResult LogGenerationCompressionSession::append(Span<const uint8_t> batch)
{
    if(state_ != State::kStreaming)
    {
        return CompressionResult::Failure(
            CompressionResultStatus::kInvalidState, "session is not writable");
    }
    try {
        auto result = compressor_->write(batch, outputCallback());

        if(!result.ok())
        {
            abandon();
        }
        return result;
    } catch (...) {
        abandon();
        throw;
    }
}

CompressionResult LogGenerationCompressionSession::finish()
{
    if(state_ != State::kStreaming)
    {
        return CompressionResult::Failure(
            CompressionResultStatus::kInvalidState, "session cannot finish twice");
    }
    try {
        auto result = compressor_->finish(outputCallback());
        if (!result.ok())
        {
            abandon();
            return result;
        }

        if (auto error = output_->durableFlush())
        {
            abandon();
            return CompressionResult::Failure(
                CompressionResultStatus::kOutputFailed, error.message(),
                result.input_bytes, result.output_bytes);
        }
        state_ = State::kReady;
        return result;
    } catch (...) {
        abandon();
        throw;
    }
}

bool LogGenerationCompressionSession::ready() const noexcept 
{ 
    return state_ == State::kReady;
}

std::error_code LogGenerationCompressionSession::closeOutput() noexcept
{
    return output_->close();
}

void LogGenerationCompressionSession::markPublished() noexcept
{
    output_->markPublished();
    state_ = State::kPublished;

}

void LogGenerationCompressionSession::abandon() noexcept
{
    if(state_ == State::kPublished)
    {
        return;
    }
    state_ = State::kDegraded;
}

std::string LogGenerationCompressionSession::tempPath() const noexcept
{
    return output_->path();
}

CompressionCallback LogGenerationCompressionSession::outputCallback()
{
    return [this](Span<const uint8_t> bytes) {
        const auto error = output_->append(bytes);
        
        return error ? CompressionCallbackResult::Failure(error.message())
            : CompressionCallbackResult::Ok();
    };
}

/**************MmapBatchTarget***************/
MmapBatchTarget::MmapBatchTarget(std::string active_path)
    :active_path_(std::move(active_path))
{

}

MmapBatchTarget::~MmapBatchTarget()
{
    closeActive();
}

LogBackendResult MmapBatchTarget::openActive()
{
    std::lock_guard<std::mutex> lock(active_mutex_);

    if(active_fd_ >= 0)
    {
        return LogBackendResult::Ok();
    }
    active_fd_ = ::open(active_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (active_fd_ < 0)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, std::strerror(errno));
    }
    active_failed_.store(false, std::memory_order_release);

    return LogBackendResult::Ok();
}

void MmapBatchTarget::closeActive() noexcept
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    if (active_fd_ >= 0)
    {
        ::close(std::exchange(active_fd_, -1));
    }
}

std::error_code MmapBatchTarget::activeSize(uint64_t& bytes) const noexcept
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    if(active_fd_ < 0)
    {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    struct stat stat{};
    if(::fstat(active_fd_, &stat) < 0)
    {
        return {errno, std::generic_category()};
    }
    if(!S_ISREG(stat.st_mode) || stat.st_size < 0)
    {
        return std::make_error_code(std::errc::invalid_argument);
    }
    bytes = static_cast<uint64_t>(stat.st_size);
    return {};
}

std::error_code MmapBatchTarget::publishCompression(uint64_t generation, const std::string& archive_path) noexcept
{
    std::string archive_path_tmp{archive_path};

    try {
        std::error_code error;
        std::lock_guard<std::mutex> lock(compress_mutex_);
        const auto it = compress_sessions_.find(generation);
        if (it == compress_sessions_.end() || !it->second->ready())
        {
            return std::make_error_code(std::errc::operation_not_permitted);
        }
        if (auto error = it->second->closeOutput())
        {
            return error;
        }

        const bool is_exists = std::filesystem::exists(archive_path, error);
        if(error)
        {
            return error;
        }
        if(is_exists)
        {
            archive_path_tmp += ".bck";
        }

        std::filesystem::rename(it->second->tempPath(), archive_path_tmp, error);
        if(error)
        {
            return error;
        }

        // rename 成功后只执行不会抛出的标记和析构，不返回伪失败。
        it->second->markPublished();
        compress_sessions_.erase(it);

    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

std::error_code MmapBatchTarget::appendActive(uint64_t generation, uint64_t sequence, Span<const uint8_t> payload)
{
    std::unique_lock<std::mutex> lock(active_mutex_);
    if(active_failed_.load(std::memory_order_acquire))
    {
        return std::make_error_code(std::errc::io_error);
    }
    if(active_fd_ < 0)
    {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }

    auto error = WriteAll(active_fd_, payload);
    if(error)
    {
        active_failed_.store(true, std::memory_order_release);
        return error;
    }
 
    return error;
}

MmapActiveRotateOutcome MmapBatchTarget::rotateActive(
const std::string& archive_log_path, MmapDeadline deadline) noexcept
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    MmapActiveRotateOutcome outcome;
    outcome.active_open = active_fd_ >= 0;

    if (!outcome.active_open
        || active_failed_.load(std::memory_order_acquire)
        || archive_log_path.empty()
        || archive_log_path == active_path_)
    {
        outcome.error = std::make_error_code(std::errc::operation_not_permitted);
        return outcome;
    }

    if (deadline != MmapDeadline::max()
        && std::chrono::steady_clock::now() >= deadline)
    {
        outcome.error = std::make_error_code(std::errc::timed_out);
        return outcome; // 排队期间预算已耗尽；尚未 close/rename。
    }

    // 关闭旧代前同步，失败保留原 fd，允许重试轮转。
    if (::fdatasync(active_fd_) != 0)
    {
        outcome.error = {errno, std::generic_category()};
        return outcome;
    }
    const int old_fd = std::exchange(active_fd_, -1);
    outcome.active_open = false;
    if (::close(old_fd) != 0)
    {
        outcome.error = {errno, std::generic_category()};
        // Linux 下不重试 close，避免误关复用 fd；未知 I/O 状态不能伪装恢复。
        active_failed_.store(true, std::memory_order_release);
        return outcome;
    }

    // active文件 ---> archive归档 重试3次
    constexpr int kInterruptedAttempts = 3;
    for(int attempt = 0; attempt < kInterruptedAttempts; ++attempt)
    {
        outcome.error = RenameMmapArchive(active_path_, archive_log_path);
        if(outcome.error != std::errc::interrupted)
        {
            break;
        }
    }
    outcome.archive_committed = !outcome.error;

    // rename 失败时是重开旧 active，成功时是打开新 active。
    // 不重建 target/session，不在这里改变 backend generation。
    for(int attempt = 0; attempt < kInterruptedAttempts; ++attempt)
    {
        active_fd_ = ::open(active_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (active_fd_ >= 0)
        {
            outcome.active_open = true;
            break;
        }
        outcome.reopen_error = {errno, std::generic_category()};
        if (outcome.reopen_error != std::errc::interrupted)
        {
            break;
        }
    }
    if (outcome.active_open)
    {
        outcome.reopen_error.clear();
    }
    else if (!outcome.error)
    {
        outcome.error = outcome.reopen_error;
    }
    return outcome;
}

std::error_code MmapBatchTarget::flushActive() noexcept
{
    // active 分支直接 write，无额外用户态缓冲；调用前已经等过 active barrier。其实这部分 do nothing
    std::lock_guard<std::mutex> lock(active_mutex_);
    if(active_fd_ < 0)
    {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    if(activeFailed())
    {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

std::error_code MmapBatchTarget::syncActive() noexcept
{
    std::lock_guard<std::mutex> lock(active_mutex_);
    if(active_fd_ < 0)
    {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    if(::fdatasync(active_fd_) != 0)
    {
        return {errno, std::generic_category()};
    }
    return {};
}

std::error_code MmapBatchTarget::continueCompression(
uint64_t generation, uint64_t sequence, Span<const uint8_t> payload)
{
    std::lock_guard<std::mutex> lock(compress_mutex_);

    if(degraded_.count(generation) != 0)
    {
        return std::make_error_code(std::errc::io_error);
    }

    try {
        auto it = compress_sessions_.find(generation);
        if(it == compress_sessions_.end())
        {
            auto session = std::make_unique<LogGenerationCompressionSession>(
                generation,
                tempCompressionPath(generation)
            );

            it = compress_sessions_.emplace(generation, std::move(session)).first;
        }

        const auto result = it->second->append(payload);
        if(!result.ok())
        {
            degraded_[generation] = result.message;
            return CompressionStatusToError(result.status);
        }

    } catch (...) {
        degraded_[generation] = "unknown compression exception";
        return std::make_error_code(std::errc::io_error);
    }

    return {};
}

std::error_code MmapBatchTarget::finishCompression(uint64_t generation)
{
    std::lock_guard<std::mutex> lock(compress_mutex_);
    const auto it = compress_sessions_.find(generation);
    if (degraded_.count(generation))
    {
        return std::make_error_code(std::errc::io_error);
    }
    if (it == compress_sessions_.end())
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto result = it->second->finish();
    if (result.ok())
    {
        return {};
    }
    degraded_[generation] = result.message;
    return CompressionStatusToError(result.status);
}

void MmapBatchTarget::setDegraded(uint64_t generation, std::string reason)
{
    std::lock_guard<std::mutex> lock(compress_mutex_);
    
    degraded_[generation] = std::move(reason);
    const auto it = compress_sessions_.find(generation);
    if(it != compress_sessions_.end())
    {
        it->second->abandon();
    }
}

bool MmapBatchTarget::degraded(uint64_t generation) const noexcept
{
    std::lock_guard<std::mutex> lock(compress_mutex_);
    return degraded_.count(generation) != 0;
}

MmapBatchGenerationProgress MmapBatchTarget::progress(uint64_t generation) const
{
    std::lock_guard<std::mutex> lock(progress_mutex_);
    const auto it = progress_.find(generation);
    return it == progress_.end() ? MmapBatchGenerationProgress{} : it->second;
}

bool MmapBatchTarget::waitProgress(uint64_t generation, 
    uint64_t last_sequence, bool include_compression, int64_t timeout_ms) const
{
    if(timeout_ms < -1)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(progress_mutex_);
    const auto ready_func = [this, generation, last_sequence, include_compression]
    {
        const auto it = progress_.find(generation);
        if(it == progress_.end())
        {
            return false;
        }
        const auto& p = it->second;
        return p.last_submitted_sequence >= last_sequence
            && p.active_completed_sequence >= last_sequence
            && (!include_compression || p.compression_completed_sequence >= last_sequence);
    };
    if(timeout_ms == -1)
    {
        progress_cv_.wait(lock, ready_func);
        return true;
    }
    return progress_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready_func);
}

void MmapBatchTarget::beginGeneration(uint64_t generation, uint64_t completed_sequence)
{
    std::lock_guard<std::mutex> lock(progress_mutex_);
    const MmapBatchGenerationProgress initial{
        completed_sequence,
        completed_sequence,
        completed_sequence
    };
    if(!progress_.emplace(generation, initial).second)
    {
        throw std::logic_error("generation already registered");
    }
}

/// @brief 更新同一代日志文件中不同批次号
void MmapBatchTarget::submitted(uint64_t generation, uint64_t sequence)
{
    std::lock_guard<std::mutex> lock(progress_mutex_);
    const auto it = progress_.find(generation);
    if (it == progress_.end()
        || sequence == 0
        || sequence - 1 != it->second.last_submitted_sequence)
    {
        // 任务已经发布，保留 slot 并拒绝后续轮转，不能伪装成入队失败。
        setActiveFailed();
        progress_cv_.notify_all();
        return;
    }
    it->second.last_submitted_sequence = sequence;

}

void MmapBatchTarget::prepareNextGeneration(uint64_t generation)
{
    std::lock_guard<std::mutex> lock(progress_mutex_);
    const auto result = progress_.try_emplace(generation, MmapBatchGenerationProgress{});
    const auto& p = result.first->second;
    if(!result.second
        && (p.last_submitted_sequence != 0 ||
        p.active_completed_sequence != 0 || p.compression_completed_sequence != 0))
    {
        throw std::logic_error("mmap batch next generation already contains batches");
    }
}

void MmapBatchTarget::retireGeneration(uint64_t generation)
{
    // 仅在双分支和 End 均结束后调用，锁顺序与 compression 路径一致。
    std::lock_guard<std::mutex> compression_lock(compress_mutex_);
    std::lock_guard<std::mutex> progress_lock(progress_mutex_);
    // session 析构只删除它自己未发布的临时文件，不删除 archive.log。
    compress_sessions_.erase(generation);
    degraded_.erase(generation);
    progress_.erase(generation);
}


void MmapBatchTarget::activeCompleted(uint64_t generation, uint64_t sequence)
{
    std::lock_guard<std::mutex> lock(progress_mutex_);
    auto& p = progress_.at(generation);
    if (sequence == p.active_completed_sequence + 1)
    {
        p.active_completed_sequence = sequence;
    }
    progress_cv_.notify_all();
}

void MmapBatchTarget::compressionCompleted(uint64_t generation, uint64_t sequence)
{
    std::lock_guard<std::mutex> lock(progress_mutex_);
    auto& p = progress_.at(generation);
    if (sequence == p.compression_completed_sequence + 1)
    {
        p.compression_completed_sequence = sequence;
    }
    progress_cv_.notify_all();
}

std::string MmapBatchTarget::tempCompressionPath(uint64_t generation) const
{
    return active_path_ + ".g" + std::to_string(generation) + ".zst.tmp";
}

} // namespace kit_muduo