/**
 * @file log_mmap_completion.h
 * @brief 日志mmap已封装批次双写操作管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-23 17:45:34
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_MMAP_COMPLETION_H__
#define __KIT_LOG_MMAP_COMPLETION_H__


#include "base/mmap_log_file_backend.h"
#include "base/noncopyable.h"
#include "base/span.h"
#include "base/compression.h"

#include <atomic>
#include <string>
#include <system_error>
#include <memory>

namespace kit_muduo {

class MmapBatchTarget;
struct MmapActiveRotateOutcome;
class ArchiveTempFile;

enum class LogMmapBranchState
{
    kPending,
    kRunning,
    kSucceeded,
    kFailed,
    kSkipped, // 压缩线程分支专用 可以选择不压缩
};

struct LogMmapBranchResult
{
    std::error_code error;
    std::string message;

    bool ok() const noexcept { return !error; }
};

/**
 * @brief MmapSlot包装器
 */
class MmapSlotLease: Noncopyable
{
public:
    explicit MmapSlotLease(MmapSlot& slot)
        :slot_(slot) { }
    MmapSlot& slot() const noexcept { return slot_; }
private:
    MmapSlot& slot_; // backend 保证 close 前所有 lease 已终止
};


class LogMmapBatchCompletion: Noncopyable
{
public:
    LogMmapBatchCompletion();

    bool startActive() noexcept;
    bool completeActive(std::error_code = {}, std::string = {}) noexcept;

    bool startCompression() noexcept;
    bool completeCompression(std::error_code ec = {}, std::string message = "") noexcept;
    bool skipCompression(std::string reason = {}) noexcept;


    LogMmapBranchState activeState() const noexcept { return active_.load(std::memory_order_acquire); }
    LogMmapBranchState compressionState() const noexcept { return compression_.load(std::memory_order_acquire); }

    LogMmapBranchResult activeResult() const;
    LogMmapBranchResult compressionResult() const;

    bool bothTerminal() const noexcept;
    bool canReleaseSlot() const noexcept;

private:
    static bool IsTerminal(LogMmapBranchState state) noexcept;

    bool complete(std::atomic<LogMmapBranchState>& state,
        LogMmapBranchResult& result,
        std::error_code error,
        std::string message) noexcept;


    bool transition(std::atomic<LogMmapBranchState>& state, LogMmapBranchState from, LogMmapBranchState to) noexcept;

private:
    mutable std::mutex result_mutex_;
    LogMmapBranchResult active_result_;
    LogMmapBranchResult compression_result_;

    std::atomic<LogMmapBranchState> active_{LogMmapBranchState::kPending};
    std::atomic<LogMmapBranchState> compression_{LogMmapBranchState::kPending};
};


class MmapSealedBatch: Noncopyable
{
public:
    using Ptr = std::shared_ptr<MmapSealedBatch>;

    MmapSealedBatch(std::shared_ptr<MmapSlotLease> lease, 
        std::shared_ptr<MmapBatchTarget> target,
        bool compression_enabled);

    Span<uint8_t> payload() const noexcept { return lease_->slot().payload().first(sealed_size_); }
    uint64_t generation() const noexcept { return generation_; }
    uint64_t sequence() const noexcept { return sequence_; }
    uint64_t epoch() const noexcept { return epoch_; }
    bool compressionEnabled() const noexcept { return compression_enabled_; }
    MmapBatchTarget& target() const noexcept { return *target_; }

    LogMmapBatchCompletion& completion() noexcept { return completion_; }

public:
    static std::shared_ptr<MmapSealedBatch> Make(
    MmapSlot& slot, std::shared_ptr<MmapBatchTarget> target, bool compression_enabled, bool recovered = false);


private:
    std::shared_ptr<MmapSlotLease> lease_;
    std::shared_ptr<MmapBatchTarget> target_;
    bool compression_enabled_;
    uint64_t epoch_;
    uint64_t generation_;
    uint64_t sequence_;
    size_t sealed_size_;
    LogMmapBatchCompletion completion_;
};

/**
 * @brief 双写进度记录 active文件+压缩文件
 */
struct MmapBatchGenerationProgress 
{
    uint64_t last_submitted_sequence{0};
    uint64_t active_completed_sequence{0};
    uint64_t compression_completed_sequence{0};
};


/**
 * @brief 日志流式压缩会话
 */
class LogGenerationCompressionSession: Noncopyable
{
public:
    enum class State {
        kStreaming, kReady, kDegraded, kFullRecompressPending, kPublished, kFailed
    };

    LogGenerationCompressionSession(uint64_t generation, std::string path);

    CompressionResult append(Span<const uint8_t> batch);
    CompressionResult finish();

    bool ready() const noexcept;
    std::error_code closeOutput() noexcept;
    void markPublished() noexcept;
    void abandon() noexcept;
    std::string tempPath() const noexcept;

    State state() const noexcept { return state_; }

private:
    CompressionCallback outputCallback();

private:
    uint64_t generation_;
    std::unique_ptr<ArchiveTempFile> output_;
    std::unique_ptr<StreamCompressor> compressor_;
    State state_{State::kStreaming};
};


/**
 * @brief 日志mmap batch写入状态管理
 */
class MmapBatchTarget: Noncopyable
{
public:
    explicit MmapBatchTarget(std::string active_path);
    ~MmapBatchTarget();

    LogBackendResult openActive();
    void closeActive() noexcept;
    std::error_code activeSize(uint64_t& bytes) const noexcept;


    std::error_code publishCompression(uint64_t generation, const std::string& archive_path) noexcept;

    bool activeFailed() const noexcept { return active_failed_.load(); }
    void setActiveFailed() noexcept { active_failed_.store(true); }

    std::error_code appendActive(uint64_t generation, uint64_t sequence, Span<const uint8_t> payload);
    MmapActiveRotateOutcome rotateActive(
    const std::string& archive_log_path, MmapDeadline deadline) noexcept;
    std::error_code flushActive() noexcept;
    std::error_code syncActive() noexcept;

    std::error_code continueCompression(uint64_t generation, uint64_t sequence, Span<const uint8_t> payload);
    std::error_code finishCompression(uint64_t generation);

    void setDegraded(uint64_t generation, std::string reason);
    bool degraded(uint64_t generation) const noexcept;

    MmapBatchGenerationProgress progress(uint64_t generation) const;
    bool waitProgress(uint64_t generation, uint64_t last_sequence,
        bool include_compression, int64_t timeout_ms) const;
    void beginGeneration(uint64_t generation, uint64_t completed_sequence = 0);
    void submitted(uint64_t generation, uint64_t sequence);
    void prepareNextGeneration(uint64_t generation);
    void retireGeneration(uint64_t generation);

    void activeCompleted(uint64_t generation, uint64_t sequence);
    void compressionCompleted(uint64_t generation, uint64_t sequence);
    const std::string& activePath() const noexcept { return active_path_; }


private:
    std::string tempCompressionPath(uint64_t generation) const;

private:
    std::string active_path_;
    int32_t active_fd_{-1};
    std::atomic_bool active_failed_{false};

    mutable std::mutex active_mutex_;
    mutable std::mutex compress_mutex_;

    mutable std::mutex progress_mutex_;
    mutable std::condition_variable progress_cv_;
    /// @brief 日志每个代次的 mmap batch进度
    std::unordered_map<uint64_t, MmapBatchGenerationProgress> progress_;
    /// @brief 日志每个代次的 流式压缩会话
    std::unordered_map<uint64_t,
        std::unique_ptr<LogGenerationCompressionSession>> compress_sessions_;
    /// @brief 日志每个代次的 降级原因
    std::unordered_map<uint64_t, std::string> degraded_;
};


} //namespace kit_muduo
#endif //__KIT_LOG_MMAP_COMPLETION_H__