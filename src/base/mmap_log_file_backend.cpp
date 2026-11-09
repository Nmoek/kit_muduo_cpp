/**
 * @file mmap_log_file_backend.cpp
 * @brief mmap映射 日志持久化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-04 15:38:22
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/mmap_log_file_backend.h"
#include "base/defer.h"
#include "base/log_file_backend.h"
#include "base/log_inner.h"
#include "base/log_mmap_batch.h"
#include "base/util.h"
#include "base/log_mmap_completion.h"

#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace kit_muduo {

namespace {

bool IsValidState(uint32_t raw) noexcept
{
    return raw <= static_cast<uint32_t>(MmapSlotState::kProcessing);
}

bool HeaderShapeValid(const MmapSlotHeader& header, uint64_t capacity) noexcept
{
    return header.magic == MmapSlotHeader::kMmapSlotMagic
        && header.version == MmapSlotHeader::kMmapSlotVersion
        && header.header_size == sizeof(MmapSlotHeader)
        && IsValidState(header.state)
        && header.payload_size <= capacity;
}


inline bool IsSameMmapRotation(const LogFileRotateRequest& a, const LogFileRotateRequest& b)
{
    return a.generation == b.generation && a.last_sequence == b.last_sequence && a.active_path == b.active_path && a.archive_log_path == b.archive_log_path && a.compression_enabled == b.compression_enabled;
}

using MmapDeadline = std::chrono::steady_clock::time_point;

inline MmapDeadline MakeMmapDeadline(int64_t timeout_ms)
{
    if (timeout_ms == -1) return MmapDeadline::max();
    if (timeout_ms < -1) throw std::invalid_argument("invalid timeout");
    const auto now = std::chrono::steady_clock::now();
    const auto limit = std::chrono::duration_cast<std::chrono::milliseconds>(
        MmapDeadline::max() - now).count();
    if (timeout_ms >= limit) return MmapDeadline::max();
    return now + std::chrono::milliseconds(timeout_ms);
}

inline int64_t RemainingMmapMs(MmapDeadline deadline)
{
    if (deadline == MmapDeadline::max()) return -1;
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return 0;
    // 底层接口粒度为毫秒；向上取整最多增加不足 1ms 的等待。
    return std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
}

/**
 * @brief 将崩溃后截断的压缩tmp文件删除
 * @param active_path 
 * @return std::error_code 
 */
std::error_code QuarantineRealtimeTemps(const std::string& active_path)
{
    const std::filesystem::path active(active_path);
    const auto directory = active.has_parent_path() ? active.parent_path() : std::filesystem::path(".");
    const std::string prefix = active.filename().string() + ".g";
    const std::string suffix = ".zst.tmp";
    std::error_code error;

    std::vector<std::filesystem::path> candidates;

    for(std::filesystem::directory_iterator it(directory, error), end;
        !error && it != end; it.increment(error))
    {
        const auto name = it->path().filename().string();
        if (name.size() <= prefix.size() + suffix.size()
        || name.compare(0, prefix.size(), prefix) != 0
        || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
        {
            continue;
        }

        const auto digits = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());

        if (!std::all_of(digits.begin(), digits.end(),
            [](char ch) { return ch >= '0' && ch <= '9'; }))
        {
            continue;
        }
        const auto status = it->symlink_status(error);
        if(error)
        {
            return error;
        }
        if(!std::filesystem::is_regular_file(status))
        {
            return std::make_error_code(std::errc::operation_not_permitted);
        }
        candidates.push_back(it->path());
    }
    if(error)
    {
        return error;
    }
    for(const auto& candidate : candidates)
    {
        std::filesystem::remove(candidate, error);
        if(error)
        {
            LOG_INNER_ERROR("log mmap cleanup old realtime temps error: %s \n", error.message().c_str());
        }
    }
    return {};
}


/**
 * @brief 重新读取当前active文件内容并恢复到流式压缩上下文中
 * @param target 
 * @param generation 
 * @param prefix_size 
 * @return std::error_code 
 */
std::error_code ReadActiveAndReCompress(MmapBatchTarget &target, uint64_t generation, uint64_t prefix_size)
{
    if(0 == prefix_size)
    {
        return {};
    }
    const int32_t fd = ::open(target.activePath().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) 
    {
        return {errno, std::generic_category()};
    }
    Defer close_reader([fd]() noexcept { 
        (void)::close(fd);
    });

    struct stat stat{};
    if(::fstat(fd, &stat) != 0)
    {
        return {errno, std::generic_category()};
    }
    if(!S_ISREG(stat.st_mode) || stat.st_size < 0
        || static_cast<uint64_t>(stat.st_size) != prefix_size)
    {
        return std::make_error_code(std::errc::state_not_recoverable);
    }
    std::array<uint8_t, 64 * 1024> buffer{};
    uint64_t remaining = prefix_size;

    while(remaining > 0)
    {
        const auto count = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        const auto n = ::read(fd, buffer.data(), count);
        if(n < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return {errno, std::generic_category()};
        }
        if(0 == n) // 文件读完了
        {
            break;
        }

        // 不是 batch，不调用 submitted/compressionCompleted，不 finish stream。
        if(auto error = target.continueCompression(generation, 0,
            Span<const uint8_t>(buffer.data(), static_cast<size_t>(n))))
        {
            return error;
        }
        remaining -= static_cast<uint64_t>(n);
    }
    return {};
}

} // namespace

/****************MmapSlot**************/

MmapSlot::~MmapSlot()
{
    close();
}

LogBackendResult MmapSlot::open(const std::string& path, uint64_t payload_capacity)
{
    if(path.empty() || 0 == payload_capacity)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "param invalid");
    }
    // 注意重复打开
    if(fd_ != -1 && MAP_FAILED != mmap_handle_)
    {
        return LogBackendResult::Ok();
    }
    file_path_ = path;
    payload_capacity_ = payload_capacity;
    mmap_size_ = AlignToCachePage(sizeof(MmapSlotHeader) + payload_capacity, GetCachePageSize());

    LOG_INNER_DEBUG("mmap backend mmap_size[%lu] payload_capacity[%lu] \n", mmap_size_, payload_capacity_);

    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, 
            std::string("open slot: ") + std::to_string(errno) + ":" + strerror(errno));
    }
    bool open_failed = false;
    Defer close_df([this, &open_failed](){
        if(!open_failed)
        {
            return;
        }
        close();
    });

    // 第一次打开/重复打开映射大小检查
    struct stat stat_buf{};
    if (::fstat(fd_, &stat_buf) != 0)
    {
        open_failed = true;
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
            std::string("stat slot: ") + std::to_string(errno) + ":" + strerror(errno));
    }

    const bool fresh = stat_buf.st_size == 0;
    if(fresh)
    {
        if(::ftruncate(fd_, static_cast<off_t>(mmap_size_)) != 0)
        {
            open_failed = true;
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                std::string("size slot: ") + std::to_string(errno) + ":" + strerror(errno));
        }

    }
    else if(static_cast<uint64_t>(stat_buf.st_size) != mmap_size_)
    {
        open_failed = true;
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
            std::string("mmap slot size mismatch"));
    }

    mmap_handle_ = ::mmap(nullptr, mmap_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if(MAP_FAILED == mmap_handle_)
    {
        mmap_handle_ = nullptr;
        open_failed = true;
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
            std::string("mmap slot: ") + std::to_string(errno) + ":" + strerror(errno));
    }

    header_ = static_cast<MmapSlotHeader*>(mmap_handle_);
    payload_ = static_cast<uint8_t*>(mmap_handle_) + sizeof(MmapSlotHeader);

    if(fresh)
    {
        *header_ = MmapSlotHeader{};
        header_->slot_epoch = 1;
        if(!syncHeader(MS_SYNC))
        {
            open_failed = true;

            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                std::string("initialize slot: ") + std::to_string(errno) + ":" + strerror(errno));
        }
    }
    else if(!HeaderShapeValid(*header_, payload_capacity_))
    {
            open_failed = true;
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "mmap slot header invalid");
    }

    return LogBackendResult::Ok();
}


void MmapSlot::close() noexcept
{
    if(MAP_FAILED != mmap_handle_)
    {
        ::munmap(mmap_handle_, mmap_size_);
    }
    if(fd_ >= 0)
    {
        const int32_t fd = std::exchange(fd_, -1);
        ::close(fd);
    }
    mmap_handle_ = MAP_FAILED;
    fd_ = -1;
    payload_capacity_ = 0;
    header_ = nullptr;
    payload_ = nullptr;
}

bool MmapSlot::writable() const noexcept
{
    return header_ && static_cast<MmapSlotState>(header_->state) == MmapSlotState::kWritable;
}

bool MmapSlot::append(Span<const uint8_t> record) noexcept
{
    if(!writable() || record.size() > remainedSize())
    {
        return false;
    }
    std::memcpy(payload_ + header_->payload_size, record.data(), record.size());
    header_->payload_size += record.size();
    return true;
}

bool MmapSlot::begin(uint64_t generation, uint64_t sequence) noexcept
{
    if(MmapSlotState::kEmpty != static_cast<MmapSlotState>(header_->state))
    {
        return false;
    }
    header_->log_generation = generation;
    header_->log_batch_sequence = sequence;
    header_->payload_size = 0;
    header_->payload_crc32 = 0;
    header_->state = static_cast<uint32_t>(MmapSlotState::kWritable);
    return syncHeader(MS_SYNC);
}

bool MmapSlot::seal() noexcept
{
    if (!header_ || header_->payload_size > payload_capacity_)
    {
        return false;
    }
    const auto state = static_cast<MmapSlotState>(header_->state);
    if (state != MmapSlotState::kWritable && state != MmapSlotState::kSealed)
    {
        return false;
    }

    // payload/length/CRC 先持久化，最后一次 header 同步才是 committed 发布点。
    /* 为什么这里要做2次同步操作:
        数据分布在多个内存页的情况下 一次性全同步, 
        异常掉电/崩溃情况可能造成"部分同步"：
        1. 头同步负载没同步
        2. 负载同步头没同步

        分2次同步，固定以头中状态为提交基准，业务负载允许丢弃，即：磁盘上看到sealed状态一定是 头+负载都同步成功
    */
    if (state == MmapSlotState::kWritable)
    {
        header_->payload_crc32 = CalcCrc32(reinterpret_cast<const unsigned char*>(payload_), header_->payload_size);
        
        if (::msync(mmap_handle_, mmap_size_, MS_SYNC) != 0)
        {
            return false;
        }

        header_->state = static_cast<uint32_t>(MmapSlotState::kSealed);
    }

    return syncHeader(MS_SYNC);
}

bool MmapSlot::markProcessing() noexcept
{
    if(!header_ || (static_cast<MmapSlotState>(header_->state) != MmapSlotState::kSealed && static_cast<MmapSlotState>(header_->state) != MmapSlotState::kProcessing))
    {
        return false;
    }
    header_->state = static_cast<uint32_t>(MmapSlotState::kProcessing);
    return syncHeader(MS_SYNC);
}

bool MmapSlot::reset(uint64_t next_epoch) noexcept
{
    if(!header_)
    {
        return false;
    }
    *header_ = MmapSlotHeader{};
    header_->slot_epoch = next_epoch;
    return syncHeader(MS_SYNC);
}

Span<uint8_t> MmapSlot::payload() const noexcept
{
    if(!header_ || header_->payload_size > payload_capacity_)
    {
        return {};
    }
    return {payload_, header_->payload_size};
}

bool MmapSlot::syncHeader(int flags) noexcept
{
    if(!mmap_handle_ || !header_)
    {
        return false;
    }

    return ::msync(mmap_handle_, AlignToCachePage(sizeof(MmapSlotHeader)), flags) == 0;
}


/**********MmapBufferedLogFileBackend*********/
MmapBufferedLogFileBackend::MmapBufferedLogFileBackend(MmapBatchScheduler& mmap_scheduler, 
    FullRecompressScheduler& recompress_scheduler,
    MmapBackendOptions options)
    :mmap_scheduler_(mmap_scheduler)
    ,compress_scheduler_(recompress_scheduler)
    ,compression_codec_suffix_(CompressionCodec::Create()->suffix())
    ,options_(std::move(options))
{

}


/**
 * @brief 注意: 不靠析构掩盖未结束任务
 */
MmapBufferedLogFileBackend::~MmapBufferedLogFileBackend()
{
    for(const auto &batch : in_flight_)
    {
        if(batch && !batch->completion().bothTerminal())
        {
            std::terminate();
        }
    }
    for(const auto &archive : pending_archives_)
    {
        std::error_code error;
        if(archive.end_result && !archive.end_result->waitFor(0, error))
        {
            std::terminate();
        }
    }
    close();
    if(target_ || slots_[0] || slots_[1])
    {
        std::terminate();
    }
}


LogBackendResult MmapBufferedLogFileBackend::open(const std::string& normalize_path)
{
    const std::string &active_path = normalize_path;
    if(active_path.empty())
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "empty active path");
    }
    if(is_open_)
    {
        return target_ && target_->activePath() == active_path
            ? LogBackendResult::Ok()
            : LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "backend path changed");
    }
    if(slot_io_failed_ || (target_ && target_->activeFailed()))
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
            "retained I/O failure requires explicit recovery");
    }
    if(active_reopen_pending_)
    {
        if(!target_ || target_->activePath() != active_path)
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "invalid fd-only reopen target");
        }
        auto result = target_->openActive();
        if(!result.ok())
        {
            return result;
        }
        uint64_t reopend_size = 0;
        if(auto error = target_->activeSize(reopend_size))
        {
            target_->closeActive();
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "read reopened active size " + error.message());
        }

        active_open_size_ = reopend_size;
        is_open_ = true;
        active_reopen_pending_ = false;
        return LogBackendResult::Ok();
    }

    if(target_)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
            "previous recovery incomplete; drain and close before cold reopen");
    }
    if (options_.slot_payload_capacity == 0
        || options_.slot_payload_capacity > std::numeric_limits<uint32_t>::max()
        || options_.batch_submit_bytes == 0
        || options_.batch_submit_bytes > options_.slot_payload_capacity
        || options_.flush_timeout.count() < -1)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "invalid mmap options");
    }

    bool opened = false;
    Defer abort_open([this, &opened]() noexcept {
        if(opened)
        {
            return;
        }
        LOG_INNER_ERROR("log mmap open error!\n");
        for(const auto& batch : in_flight_)
        {
            if (batch && !batch->completion().bothTerminal())
            {
                std::terminate();
            }
        }
        for(auto& batch : in_flight_)
        {
            batch.reset();
        }
        target_.reset();
        for(auto& slot : slots_)
        {
            slot.reset();
        }
        is_open_ = false;
    });

    try {
        if (!chechkAndCreateLogPath(active_path))
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "cannot create active directory");
        }
        auto result = openSlots(active_path);
        if (!result.ok())
        {
            return result;
        }

        std::vector<RecoveredSlot> committed;
        result = inspectSlots(committed);
        if(!result.ok())
        {
            return result;
        }

        generation_ = committed.empty() ? 1 : committed.front().generation;
        last_sequence_ = committed.empty() ? 0 : committed.front().sequence - 1;
        uint64_t max_epoch = 0;
        for(const auto &slot : slots_)
        {
            max_epoch = std::max(max_epoch, slot->header().slot_epoch);
        }
        if(max_epoch == std::numeric_limits<uint64_t>::max())
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "slot epoch exhausted");
        }
        next_epoch_ = max_epoch + 1;

        for(auto &slot : slots_)
        {
            if(static_cast<MmapSlotState>(slot->header().state) != MmapSlotState::kWritable)
            {
                continue;
            }
            if(next_epoch_ == std::numeric_limits<uint64_t>::max())
            {
                return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "slot epoch exhausted");
            }
            if(!slot->reset(next_epoch_))
            {
                slot_io_failed_ = true;
                return LogBackendResult::Failure(LogBackendStatus::kSyncFailed,
                    "reset uncommitted slot");
            }
            ++next_epoch_;
        }

        target_ = std::make_shared<MmapBatchTarget>(active_path);
        result = target_->openActive();
        if(!result.ok())
        {
            return result;
        }
        if(auto error = target_->activeSize(active_open_size_))
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "read active prefix size");
        }
        target_->beginGeneration(generation_, last_sequence_);

        if(options_.compression_enabled)
        {
            auto error = QuarantineRealtimeTemps(active_path);
            if(!error)
            {
                error = importExistingActive();
            }
            if(error)
            {
                // 若记录降级本身抛异常，则外层捕获并终止启动，不能假装完整流。
                target_->setDegraded(generation_, "recovery compression: " + error.message());
                LOG_INNER_WARN("mmap recovery compression degraded: %s\n", error.message().c_str());
            }
        }

        result = replayInspected(committed);
        if (!result.ok())
        {
            return result;
        }
        uint64_t recovered_size = 0;
        if(auto error = target_->activeSize(recovered_size))
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "read recovered active size");
        }

        result = reclaimCompleted();
        if (!result.ok())
        {
            return result;
        }
        active_open_size_ = recovered_size;
        // 恢复任务已回收，两个 slot 均可供后续 begin 使用。
        writable_index_ = 0; 
        active_reopen_pending_ = false;
        is_open_ = true;
        opened = true;
        return LogBackendResult::Ok();

    } catch(const std::exception &e) {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, e.what());

    } catch(...) {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "unknown mmap recovery failure");

    }
}

void MmapBufferedLogFileBackend::close() noexcept
{
    pollArchiveCompletions();
    if(hasPendingArchives())
    {
        // 正常关闭前必须已 join 并完成旧代收尾；保留 target/映射供失败恢复。
        LOG_INNER_WARN("mmap close deferred: pending archive cleanup\n");
        return;
    }
    // 先确认全部 lease 已终态，再关闭 fd/unmap；不能检查一半就开始释放。
    for(const auto& batch : in_flight_)
    {
        if(batch && !batch->completion().bothTerminal()) 
        {
            LOG_INNER_WARN("mmap in flight batch not terminal\n");
            return;
        }
    }
    for (const auto& slot : slots_)
    {
        if (is_open_ && slot && slot->header().payload_size != 0
            && static_cast<MmapSlotState>(slot->header().state) == MmapSlotState::kWritable)
        {
            LOG_INNER_ERROR("mmap close deferred: flush writable tail first\n");
            return;
        }
    }
    // 只释放进程资源，不 reset 失败/未提交的 committed slot，不删除原始文件。
    if(target_)
    {
        target_->closeActive();
    }
    for(auto& batch : in_flight_)
    {
        batch.reset();
    }
    target_.reset();
    for(auto &slot : slots_)
    {
        slot.reset();
    }
    active_reopen_pending_ = false;
    slot_io_failed_ = false;
    is_open_ = false;
}

LogBackendResult MmapBufferedLogFileBackend::append(std::string_view bytes)
{
    if(!is_open_ || !target_ || target_->activeFailed() || slot_io_failed_)
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap backend is unavailable");
    }
    if(bytes.empty())
    {
        return LogBackendResult::Ok();
    }

    if(bytes.size() > options_.slot_payload_capacity)
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
            "record exceeds slot capacity");
    }
    if(bytes.size() > std::numeric_limits<uint64_t>::max() - active_open_size_)
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "generation byte count overflow");
    }


    // 回收mmap batch inflight
    auto result = reclaimCompleted();
    if(!result.ok())
    {
        return result;
    }

    // 如果上一批已封口、但之前提交被拒绝，先重试提交。
    if(MmapSlotState::kSealed == static_cast<MmapSlotState>(slots_[writable_index_]->header().state))
    {
        const auto retried = sealWritable();
        if(!retried.ok())
        {
            // 此次新的日志数据尚未进入 backend。
            return retried;
        }
    }
    if(!acquireWritable(options_.flush_timeout.count()))
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap slot no writable");
    }
    // 如果当前 slot 剩余空间放不下整条日志，先封口当前批，再换 slot。
    if(bytes.size() > slots_[writable_index_]->remainedSize())
    {
        result = sealWritable();
        if(!result.ok())
        {
            return result;
        }
        if(!acquireWritable(options_.flush_timeout.count()))
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "waiting for mmap slot failed");
        }
    }

    auto& writable_slot = *slots_[writable_index_];
    if(!writable_slot.append({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}))
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
            "mmap slot append failed");
    }
    result = LogBackendResult::Ok();
    result.requested_bytes = bytes.size();
    result.written_bytes = bytes.size();

    // 注意: 如果本条日志已经复制进 slot，随后封口或调度提交失败，backend 会保留这批数据，并返回本条已接受的结果
    if(writable_slot.header().payload_size >= options_.batch_submit_bytes)
    {
        // 当前记录已经被 backend 接受。投递失败保留 sealed batch，由下次 append/flush 重试，不能返回“记录未接受”而让调用者重复写入。
        const auto sealed = sealWritable();
        if(!sealed.ok())
        {
            LOG_INNER_ERROR("mmap accepted record; seal deferred: %s\n", sealed.message.c_str());
        }
    }
    return result;
}

LogBackendResult MmapBufferedLogFileBackend::flush() noexcept
{
    return flushFor(options_.flush_timeout.count());
}

#if 0
LogBackendResult MmapBufferedLogFileBackend::rotate(const LogFileRotateRequest& request, int64_t timeout_ms) noexcept
{
    try {
        const auto deadline = MakeMmapDeadline(timeout_ms);
        if(!rotate_ctx_)
        {
            if (!is_open_ || !target_ || !slots_[0] || !slots_[1])
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap backend is not open");
            }
            if (request.generation != generation_
                || request.last_sequence != last_sequence_
                || request.active_path != target_->activePath()
                || request.active_path.empty() 
                || request.archive_log_path.empty()
                || request.active_path == request.archive_log_path
                || request.compression_enabled != options_.compression_enabled)
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "invalid rotation request");
            }
            if (generation_ == std::numeric_limits<uint64_t>::max())
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "generation exhausted");
            }
            MmapRotateContext context;
            context.request = request;
            if(request.compression_enabled)
            {
                const auto suffix = compression_codec_.suffix();
                if (suffix.empty() || suffix.find_first_of("/\\") != std::string::npos || suffix != compress_scheduler_.compressSuffix())
                {
                    return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "invalid compression suffix");
                }
                context.archive_compression_path = request.archive_log_path + "." + suffix;
            }
            // 先为可能的异步移交预留位置。之后只 erase，不增加其他元素。
            if (pending_archives_.size() == pending_archives_.max_size())
            {
                return  LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "archive bookkeeping exhausted");
            }
            pending_archives_.reserve(pending_archives_.size() + 1);
            // 任何外部副作用之前固定请求。
            rotate_ctx_.emplace(std::move(context)); 
        }
        else if(!IsSameMmapRotation(rotate_ctx_->request, request))
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "retry the original rotation request");
        }
        auto& context = *rotate_ctx_;
        const auto old_generation = context.request.generation;

        auto active_result = [this, &context, old_generation, deadline]() -> LogBackendResult {
            for(;;)
            {
                switch(context.stage)
                {
                    case MmapBackendRotateStage::kSealPending:
                    {
                        auto result = sealWritable();
                        if(!result.ok())
                        {
                            return result;
                        }
                        context.final_sequence = last_sequence_;
                        context.stage = MmapBackendRotateStage::kSealed;
                        break;
                    }
                    case MmapBackendRotateStage::kSealed:
                    {
                        if(!target_->waitProgress(old_generation, context.final_sequence, true, RemainingMmapMs(deadline)))
                        {
                            return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "mmap batch generation barrier timeout");
                        }
                        if(target_->activeFailed())
                        {
                            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "active file failed; mmap slots retained");
                        }
                        const auto reclaimed = reclaimCompleted();
                        if (!reclaimed.ok())
                        {
                            return reclaimed;
                        }
                        // progress 通知必须晚于 completion；不能把这个检查替换为强行 reset。
                        for (const auto& batch : in_flight_)
                        {
                            if(batch)
                            {
                                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
                                "mmap batch barrier/completion invariant violated");
                            }
                        }
                        context.stage = MmapBackendRotateStage::kBatchesDrained;
                        break;
                    }
                    case MmapBackendRotateStage::kBatchesDrained:
                    {
                        target_->prepareNextGeneration(old_generation + 1);
                        while(context.reset_slot_index < slots_.size())
                        {
                            auto &slot = slots_[context.reset_slot_index];
                            const auto state = static_cast<MmapSlotState>(slot->header().state);
                            if((MmapSlotState::kEmpty != state && !slot->writable())
                                || slot->header().payload_size != 0)
                            {
                                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
                                    "nonempty slot before generation commit");
                            }
                            if (next_epoch_ == std::numeric_limits<uint64_t>::max())
                            {
                                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "slot epoch exhausted");
                            }
                            if (!slot->reset(next_epoch_))
                            {
                                slot_io_failed_ = true;
                                return LogBackendResult::Failure(LogBackendStatus::kSyncFailed,
                                    "reset empty generation slot failed");
                            }
                            ++next_epoch_;
                            ++context.reset_slot_index;
                        }

                        submitRotationEnd(context); // 只尝试一次；不 wait、不返回压缩错误。

                        if (auto error = target_->flushActive())
                        {
                            return LogBackendResult::Failure(LogBackendStatus::kFlushFailed,
                                "log active flush error " + error.message());
                        }

                        target_->closeActive();
                        is_open_ = false;
                        context.stage = MmapBackendRotateStage::kActiveClosed;
                        break;
                    }
                    case MmapBackendRotateStage::kActiveClosed:
                    {
                        std::error_code error;
                        std::filesystem::rename(context.request.active_path, context.request.archive_log_path, error);
                        if(error)
                        {
                            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "log active file rename error" + error.message());
                        }
                        context.stage = MmapBackendRotateStage::kActiveRenamed;
                        generation_ = old_generation + 1;
                        last_sequence_ = 0;
                        active_open_size_ = 0;
                        slot_io_failed_ = false;
                        break;
                    }
                    case MmapBackendRotateStage::kActiveRenamed:
                    {
                        auto result = target_->openActive();
                        if (!result.ok())
                        {
                            return result;
                        }
                        is_open_ = true;
                        context.stage = MmapBackendRotateStage::kRotateFinished;
                        break;
                    }
                    case MmapBackendRotateStage::kRotateFinished:
                    {
                        return LogBackendResult::Ok();
                    }
                    default:
                        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap backend stage invalid");
                }
            }
        }();

        // 注意 这里不能放进状态机 active文件轮转成功与否都需要检查一下压缩状态 并做压缩补偿动作
        (void)finishRotationArchive(context);

        if(active_result.ok())
        {
            if (!rotate_ctx_->generation_retired)
            {
                // 已预留容量，且元素移动不抛异常；这里只移交，不再次检查 End。
                pending_archives_.push_back(std::move(context));
            }
            rotate_ctx_.reset();
        }

        return active_result;

    }catch (const std::exception& error) {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, error.what());
    } catch (...){
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "unknown mmap rotation failure");
    }
}
#else

LogBackendResult MmapBufferedLogFileBackend::rotate(const LogFileRotateRequest& request, int64_t timeout_ms) noexcept
{
    try {
        const auto deadline = MakeMmapDeadline(timeout_ms);
        pollArchiveCompletions();

        if (!is_open_ || !target_ || !slots_[0] || !slots_[1])
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap backend is not open");
        }
        if (request.generation != generation_
            || request.last_sequence != last_sequence_
            || request.active_path != target_->activePath()
            || request.active_path.empty() 
            || request.archive_log_path.empty()
            || request.active_path == request.archive_log_path
            || request.compression_enabled != options_.compression_enabled)
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "invalid rotation request");
        }
        if (generation_ == std::numeric_limits<uint64_t>::max())
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "generation exhausted");
        }

        MmapRotateContext context;
        context.request = request;
        if(request.compression_enabled)
        {

            if (compression_codec_suffix_.empty() || compression_codec_suffix_.find_first_of("/\\") != std::string::npos || compression_codec_suffix_ != compress_scheduler_.compressSuffix())
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "invalid compression suffix");
            }
            context.archive_compression_path = request.archive_log_path + "." + compression_codec_suffix_;
        }
        // 先为可能的异步移交预留位置。之后只 erase，不增加其他元素。
        if (pending_archives_.size() == pending_archives_.max_size())
        {
            return  LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "archive bookkeeping exhausted");
        }
        pending_archives_.reserve(pending_archives_.size() + 1);

        // 压缩收尾任务 active轮转末尾投递
        auto completed = std::make_shared<MmapActiveRotateResult>();
        MmapActiveRotateTask rotate_task{
            target_,
            request.archive_log_path,
            deadline,
            completed,
        };
        Defer archive_guard([this, &context]() noexcept
        {
            if(!context.archive_committed)
            {
                return;
            }
            // 即使新 active 打开失败，旧文本已经归档，也独立触发压缩收尾。
            submitRotationEnd(context);
            (void)finishRotationArchive(context);
            if(!context.generation_retired)
            {
                // 已预留一个元素；移动不抛异常。End 尚未完成不等于失败。
                pending_archives_.push_back(std::move(context));
            }
        });

        auto result = sealWritable();
        if(!result.ok())
        {
            return result;
        }
        context.final_sequence = last_sequence_;

        if(!target_->waitProgress(generation_, context.final_sequence, true, RemainingMmapMs(deadline)))
        {
            return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "archive bookkeeping exhausted");
        }
        if(target_->activeFailed())
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "active failed; slots retained");
        }
        result = reclaimCompleted();
        if (!result.ok())
        {
            return result;
        }

        for(const auto &batch : in_flight_)
        {
            if (batch)
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
                    "batch barrier/completion invariant violated");
            }
        }

        target_->prepareNextGeneration(generation_ + 1);
        for(auto &slot : slots_)
        {
            const auto state = static_cast<MmapSlotState>(slot->header().state);
            // 文件轮转时 mmap slot必须为空
            if((MmapSlotState::kEmpty != state && !slot->writable()) || 0 != slot->header().payload_size)
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "slot not empty before rotation");
            }

            if(next_epoch_ == std::numeric_limits<uint64_t>::max())
            {
                return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "slot epoch exhausted");
            }

            if(!slot->reset(next_epoch_))
            {
                slot_io_failed_ = true;
                return LogBackendResult::Failure(LogBackendStatus::kSyncFailed, "slot reset failed");
            }
            ++next_epoch_;
        }
        slot_io_failed_ = false;

        if(deadline != MmapDeadline::max() && std::chrono::steady_clock::now() >= deadline)
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "active rotation admission timeout");
        }

        if(!mmap_scheduler_.submitActiveRotate(std::move(rotate_task)))
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "active rotation task rejected");
        }

        // 已受理后不能超时逃逸；worker 必须发布确定结果，不留下后台 active 变更。
        const auto outcome = completed->wait();
        is_open_ = outcome.active_open;
        active_reopen_pending_ = !is_open_ && !target_->activeFailed();
        // !! 特别重要: 这个条件决定了是否要压缩收尾
        context.archive_committed = outcome.archive_committed;

        if(outcome.archive_committed)
        {
            ++generation_;
            last_sequence_ = 0;
            active_open_size_ = 0;
        }
        if(outcome.error)
        {
            auto failure = LogBackendResult::Failure(outcome.archive_committed
                    ? LogBackendStatus::kOpenFailed : LogBackendStatus::kWriteFailed,
                "active rotation error " + outcome.error.message());
            if(outcome.reopen_error && outcome.reopen_error != outcome.error)
            {
               failure.message += "; reopen: " + outcome.reopen_error.message();
            }
            return failure;
        }

        return LogBackendResult::Ok();
        
    }catch (const std::exception& e) {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, e.what());
    } catch (...){
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "unknown mmap rotation failure");
    }
   
}


#endif

bool MmapBufferedLogFileBackend::recompress(std::string archive_path) noexcept
{
    if(archiveInUse(archive_path))
    {
        return false;
    }
    MmapRotateContext ctx;
    ctx.request.archive_log_path = std::move(archive_path);
    ctx.archive_compression_path = ctx.request.archive_log_path + "." + compression_codec_suffix_;
    return scheduleRotationCompensation(ctx);
}


LogBackendResult MmapBufferedLogFileBackend::flushFor(int64_t timeout_ms) noexcept
{
    try {
        // 即使 active 仍待 reopen，也收取已归档旧代的结果。
        pollArchiveCompletions(); 

        if (!is_open_ || !target_)
        {
            return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "mmap backend is not open");
        }
        // 只要flush就需要将slot中的数据全部刷新到active文件中
        auto result = sealWritable();
        if(!result.ok())
        {
            return result;
        }
        // 压缩失败也属于终态，是否成功由 active 分支决定。
        if(!target_->waitProgress(generation_, last_sequence_, true, timeout_ms))
        {
            return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "batch flush barrier timeout");
        }
        if(target_->activeFailed())
        {
            return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "active branch failed; slot retained");
        }
        if(auto error = target_->syncActive())
        {
            return LogBackendResult::Failure(LogBackendStatus::kSyncFailed, "log active file sync error " + error.message());
        }
        return reclaimCompleted();

    } catch(const std::exception &e) {
        return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, e.what());

    } catch(...) {
        return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "unknown mmap flush failure");

    }

}

void MmapBufferedLogFileBackend::pollArchiveCompletions() noexcept
{
    if(!target_)
    {
        return;
    }

    auto it = pending_archives_.begin();
    while(it != pending_archives_.end())
    {
        if(finishRotationArchive(*it))
        {
            it = pending_archives_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool MmapBufferedLogFileBackend::archiveInUse(const std::string& path) const noexcept
{
    const auto matches = [&path](const MmapRotateContext &context) {
        return path == context.request.archive_log_path
            || path == context.archive_compression_path;
    };

    for (const auto& context : pending_archives_)
    {
        if (matches(context))
        {
            return true;
        }
    }
    // TODO 补偿接受后所有权已移交给 full worker；其在途查询见 12.9。
    return compress_scheduler_.archiveInUse(path);
}

bool MmapBufferedLogFileBackend::hasPendingArchives() const noexcept
{
    return !pending_archives_.empty();
}

LogBackendResult MmapBufferedLogFileBackend::openSlots(const std::string& active_path)
{
    // HACK 暂时不去除后缀名 否则造成不同路径却指向同一mmap映射
    // auto base = std::filesystem::path(active_path);
    // base.replace_extension(); // 去除后缀拓展名 包括'.'

    const auto paths = MmapSlotPaths::MakePaths(active_path);
    std::array<std::unique_ptr<MmapSlot>, 2> mapped;
    for(size_t i = 0; i < slots_.size(); ++i)
    {
        mapped[i] = std::make_unique<MmapSlot>();
        auto result = mapped[i]->open(paths.path[i], options_.slot_payload_capacity);
        if (!result.ok())
        {
            return result;
        }
        LOG_INNER_DEBUG("open mmap file: %s \n", paths.path[i].c_str());
    }
    slots_ = std::move(mapped);
    writable_index_ = 0;
    return LogBackendResult::Ok();
}

LogBackendResult MmapBufferedLogFileBackend::sealWritable()
{
    auto& slot = *slots_[writable_index_];
    auto& batch = in_flight_[writable_index_];
    const auto state = static_cast<MmapSlotState>(slot.header().state);

    if (batch && batch->generation() == generation_ && batch->sequence() <= last_sequence_)
    {
        return LogBackendResult::Ok();
    }
    if(MmapSlotState::kProcessing == state)
    {
        // 该 slot 已被接受，不能再次提交。
        return LogBackendResult::Ok(); 
    }

    if(!batch)
    {
        // 状态为空 以及有效数据为空都不进行封存
        if(MmapSlotState::kEmpty == state)
        {
            return LogBackendResult::Ok();
        }
        if(MmapSlotState::kWritable == state && slot.header().payload_size == 0)
        {
            return LogBackendResult::Ok();
        }

        if(state != MmapSlotState::kWritable && state != MmapSlotState::kSealed)
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap slot invalid state");
        }
        if (last_sequence_ == std::numeric_limits<uint64_t>::max()
            || slot.header().log_generation != generation_
            || slot.header().log_batch_sequence != last_sequence_ + 1)
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "mmap batch generation/sequence invalid");
        }

        // mmap slot 状态切换并进行封存
        if(!slot.seal())
        {
            slot_io_failed_ = true;

            return LogBackendResult::Failure(LogBackendStatus::kSyncFailed,
                "mmap batch commit failed");
        }
        batch = MmapSealedBatch::Make(slot, target_, options_.compression_enabled); 
    }
    if(!mmap_scheduler_.submitBatch(batch))
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
            "batch rejected; sealed slot retained");
    }
    last_sequence_ = batch->sequence();
    slot.markProcessing();
    slot_io_failed_ = false;
    writable_index_ ^= 1u;

    return LogBackendResult::Ok();
}

bool MmapBufferedLogFileBackend::acquireWritable(int64_t timeout_ms)
{
    const auto deadline = MakeMmapDeadline(timeout_ms);

    for (;;)
    {
        if(slot_io_failed_ || target_->activeFailed())
        {
            return false;
        }
        if(!reclaimCompleted().ok())
        {
            return false;
        }

        auto& slot = *slots_[writable_index_];
        if(in_flight_[writable_index_])
        {
            const auto sequence = in_flight_[writable_index_]->sequence();

            if (!target_->waitProgress(generation_, sequence, true, RemainingMmapMs(deadline)))
            {
                return false;
            }
            continue;
        }
        const auto state = static_cast<MmapSlotState>(slot.header().state);
        if(MmapSlotState::kWritable == state)
        {
            return slot.header().log_generation == generation_;
        }
        if(state != MmapSlotState::kEmpty
            || last_sequence_ == std::numeric_limits<uint64_t>::max())
        {
            return false;
        }
        if(!slot.begin(generation_, last_sequence_ + 1))
        {
            slot_io_failed_ = true;
            return false;
        }
        return true;
    }
}


LogBackendResult MmapBufferedLogFileBackend::reclaimCompleted()
{
    for(size_t i = 0; i < in_flight_.size(); ++i)
    {
        auto& batch = in_flight_[i];
        if(!batch || !batch->completion().canReleaseSlot())
        {
            continue;
        }
        if (next_epoch_ == std::numeric_limits<uint64_t>::max())
        {
            return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "slot epoch exhausted");
        }
        if(!slots_[i]->reset(next_epoch_))
        {
            return LogBackendResult::Failure(LogBackendStatus::kSyncFailed, "reset slot");
        }
        // active 失败：保留 batch 和 committed slot，等待恢复，绝不 reset。
        ++next_epoch_;
        batch.reset();
    }
    return LogBackendResult::Ok();
}

LogBackendResult MmapBufferedLogFileBackend::inspectSlots(std::vector<RecoveredSlot>& committed)
{
    committed.clear();
    committed.reserve(slots_.size());
    for(size_t i = 0;i < slots_.size();++i)
    {
        if(!slots_[i])
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "unmapped recovery slot");
        }
        const auto &slot = *slots_[i];
        const auto &header = slot.header();
        if(!HeaderShapeValid(header, slot.payloadCapacity()))
        {
            LOG_INNER_ERROR("mmap recovery: invalid header at slot %zu\n", i);
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "invalid slot header; original files retained");
        }
        const auto state = static_cast<MmapSlotState>(header.state);
        if(MmapSlotState::kEmpty == state)
        {
            if(header.payload_size != 0)
            {
                return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                    "empty slot has payload; original files retained");
            }
            continue;
        }
        if(MmapSlotState::kWritable == state)
        {
            continue; // 此时还不 reset。
        }

        // 如果处于 sealed processing 状态说明一定存在有效数据
        if (header.payload_size == 0
            || header.payload_size > std::numeric_limits<uint32_t>::max()
            || 0 == header.log_generation
            || 0 == header.log_batch_sequence
            || header.log_generation == std::numeric_limits<uint64_t>::max()
            || header.log_batch_sequence == std::numeric_limits<uint64_t>::max())
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "invalid committed slot metadata; original files retained");
        }
        const auto payload = slot.payload();
        const auto crc = CalcCrc32(payload.data(), static_cast<uint32_t>(payload.size()));
        if(crc != header.payload_crc32)
        {
            LOG_INNER_ERROR("mmap recovery: CRC mismatch at slot %zu\n", i);
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "slot CRC mismatch; original files retained");
        }

        committed.push_back({
            i,
            header.log_generation,
            header.log_batch_sequence
        });
    }
    std::sort(committed.begin(), committed.end(),
        [](const RecoveredSlot& left, const RecoveredSlot& right) {
            return left.sequence < right.sequence;
        });

    for (size_t i = 1; i < committed.size(); ++i)
    {
        // 如果发现2个slot中日志代次不同 
        // 或 同一批代次中前后批次不连贯
        // 则都选择报错返回
        if (committed[i].generation != committed.front().generation
            || committed[i].sequence != committed[i - 1].sequence + 1)
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "recovery generation mismatch or sequence gap; files retained");
        }
    }
    return LogBackendResult::Ok();
}

LogBackendResult MmapBufferedLogFileBackend::replayInspected(const std::vector<RecoveredSlot>& committed)
{
    const auto deadline = MakeMmapDeadline(options_.flush_timeout.count());
    const uint64_t baseline_sequence = last_sequence_;
    auto result = LogBackendResult::Ok();

    try {
        for(auto &item : committed)
        {
            if (item.generation != generation_ || item.sequence != last_sequence_ + 1)
            {
                result = LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                    "invalid recovery submission order");
                break;
            }
            if (deadline != MmapDeadline::max() &&
                std::chrono::steady_clock::now() >= deadline)
            {
                result = LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                    "recovery admission timeout");
                break;
            }

            // 使用恢复链路重建slot sealed batch
            auto& slot = *slots_[item.index];
            auto batch = MmapSealedBatch::Make(slot, target_, options_.compression_enabled, true);
            if(!mmap_scheduler_.submitBatch(batch))
            {
                result = LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                    "recovery batch rejected; committed slot retained");
                break;
            }

            // true 之后不可伪报未受理；这些赋值不分配，也不抛异常。
            in_flight_[item.index] = std::move(batch);
            last_sequence_ = item.sequence;
            if(!slot.markProcessing())
            {
                slot_io_failed_ = true;
                result = LogBackendResult::Failure(LogBackendStatus::kSyncFailed,
                    "mark recovery batch processing");
                break;
            }
        }
    } catch(const std::exception &e) {
        result = LogBackendResult::Failure(LogBackendStatus::kOpenFailed, e.what());

    } catch(...) {
        result = LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "recovery submission failed");
    }

    if(last_sequence_ != baseline_sequence)
    {
        // 即使后一个 batch 拒绝/同步失败，也必须等前面已经接受的任务结束。
        // 正常 completion/progress 契约失效属于不可安全回收的内部致命错误。
        // 注意混销点：这里等待压缩=true并不会 

        if(!target_->waitProgress(generation_, last_sequence_, true, -1))
        {
            LOG_INNER_EXCPTION("log mmap replay slot exception\n");
            std::terminate();
        }
    }
    if(!result.ok())
    {
        return result;
    }
    if(target_->activeFailed())
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
            "recovery active write failed; committed slots retained");
    }
    for (const auto& item : committed)
    {
        const auto& batch = in_flight_[item.index];
        if(!batch || !batch->completion().canReleaseSlot())
        {
            return LogBackendResult::Failure(LogBackendStatus::kOpenFailed,
                "recovery progress/completion invariant violated");
        }
    }
    // 这里只确认处理终态；open 读完恢复后长度后再统一回收。
    return LogBackendResult::Ok();
}


std::error_code MmapBufferedLogFileBackend::importExistingActive() noexcept
{
    if(!options_.compression_enabled || active_open_size_ == 0)
    {
        return {};
    }
    try {
        return ReadActiveAndReCompress(*target_, generation_, active_open_size_);
    } catch (const std::system_error& error) {
        return error.code();
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
}



bool MmapBufferedLogFileBackend::scheduleRotationCompensation(const MmapRotateContext& context) noexcept
{
    try {
        struct stat stat{};
        if (0 == ::lstat(context.archive_compression_path.c_str(), &stat) || errno != ENOENT)
        {
            // 已有压缩归档（包括悬空符号链接）时不安排覆盖任务。
            return false; 
        }
        FullRecompressTask task;
        task.task_id = FullRecompressTask::NextTaskId();
        task.generation = context.request.generation;
        task.archive_log_path = context.request.archive_log_path;
        task.archive_compression_path = context.archive_compression_path;
        return compress_scheduler_.submit(std::move(task));
    } catch (...) {
        return false;
    }
}

void MmapBufferedLogFileBackend::submitRotationEnd(MmapRotateContext& context) noexcept
{
    if(context.end_decided)
    {
        return;
    }
    context.end_decided = true;

    if(!context.request.compression_enabled)
    {
        return;
    }
    try {

        // 本代日志压缩已降级则返回
        if(target_->degraded(context.request.generation))
        {
            context.is_compress_compensation = true;
            context.compression_error = std::make_error_code((std::errc::io_error));
            return;
        }
        auto result = std::make_shared<MmapBatchResult>();
        MmapBatchEndTask task{
            target_,
            context.request.generation,
            context.final_sequence,
            result,
        };

        // 压缩结束任务提交成功就返回
        if(mmap_scheduler_.submitBatchEnd(std::move(task)))
        {
            context.end_result = std::move(result);
            context.disposition = MmapArchiveDisposition::kPending;
            return;
        }
        context.compression_error = std::make_error_code(std::errc::resource_unavailable_try_again);

    } catch(...) {
        context.compression_error = std::make_error_code(std::errc::io_error);
    }
    context.is_compress_compensation = true;
}

bool MmapBufferedLogFileBackend::finishRotationArchive(MmapRotateContext& context) noexcept
{
    if(context.generation_retired)
    {
        return true;
    }
    try {
        if(!context.archive_handled)
        {
            if(context.request.compression_enabled)
            {
                if(context.end_result)
                {
                    // 优先查看已接受的 End，不能因 degraded 而越过仍在运行的任务。
                    std::error_code error;
                    if(!context.end_result->waitFor(0, error))
                    {
                        context.disposition = MmapArchiveDisposition::kPending;
                        recordRotationReport(context);
                        // 注意: 这里压缩任务超时返回不代表是失败 等待下一个轮转周期里进行检查
                        // 不能 重试/压缩补偿/删除 日志归档
                        return false;
                    }
                    context.compression_error = error;
                    // context.is_compress_compensation = static_cast<bool>(error);
                }
    #if 0
                if(!context.is_compress_compensation)
                {
                    context.compression_error = target_->publishCompression(
                        context.request.generation, context.archive_compression_path);

                    context.is_compress_compensation =
                        static_cast<bool>(context.compression_error);
                }

                // HACK 外部LogFileSink已经有压缩补偿  这里暂时去掉
                if(context.is_compress_compensation)
                {
                    const bool is_conflict = context.compression_error  == std::errc::file_exists;

                    const bool accepted = !is_conflict && scheduleRotationCompensation(context);

                    context.disposition = accepted ? MmapArchiveDisposition::kCompensationQueued : MmapArchiveDisposition::kRawRetained;
                }
                else
                {
                    context.disposition = MmapArchiveDisposition::kRealtimePublished;
                    if(::unlink(context.request.archive_log_path.c_str()) != 0 && errno != ENOENT)
                    {
                        context.cleanup_error = {errno, std::generic_category()};
                    }
                }
    #else
                if(!context.compression_error)
                {
                    context.compression_error = target_->publishCompression(context.request.generation, context.archive_compression_path);

                    if(context.compression_error)
                    {
                        context.disposition = MmapArchiveDisposition::kRawRetained;
                    }
                    else
                    {
                        context.disposition = MmapArchiveDisposition::kRealtimePublished;
                        if(::unlink(context.request.archive_log_path.c_str()) != 0 && errno != ENOENT)
                        {
                            context.cleanup_error = {errno, std::generic_category()};
                        }
                    }
                }
                else
                {
                    context.disposition = MmapArchiveDisposition::kRawRetained;
                }

    #endif
            }

            context.archive_handled = true;
        }

        // 只有 End 已终态，或明确未提交 End，才允许释放旧代 session/progress。
        // 回收失败保留 archive_handled，后续只重试回收，不重复发布/补偿。
        target_->retireGeneration(context.request.generation);
        context.generation_retired = true;
        recordRotationReport(context);

    } catch(...) {
        context.cleanup_error = std::make_error_code(std::errc::io_error);
        recordRotationReport(context);

        // 归档错误不能改写 active rotate 的返回值。
        return false; 
    }

    return true;
}

void MmapBufferedLogFileBackend::recordRotationReport(const MmapRotateContext& context) noexcept
{
    // 较老归档晚完成时，不覆盖 最近一次轮转 报告。
    if (context.request.generation >= last_rotation_report_.generation)
    {
        last_rotation_report_ = {
            context.request.generation,
            context.disposition,
            context.compression_error,
            context.cleanup_error
        };
    }
}



} // namespace kit_muduo