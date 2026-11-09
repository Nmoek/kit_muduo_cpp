/**
 * @file mmap_log_file_backend.h
 * @brief mmap映射 日志持久化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-04 15:27:57
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_MMAP_LOG_FILE_BACKEND_H__
#define __KIT_MMAP_LOG_FILE_BACKEND_H__

#include "base/log_mmap_batch.h"
#include "base/noncopyable.h"
#include "base/log_file_backend.h"

#include <string>

namespace kit_muduo {

struct RecoveredSlot;

enum class MmapSlotState 
{
    kEmpty,
    kWritable,
    kSealed,
    kProcessing
};

struct MmapSlotHeader 
{
    static constexpr uint32_t kMmapSlotMagic = 0x4B4D424C; // KMBL
    static constexpr uint16_t kMmapSlotVersion = 1;

    /// @brief 头标识魔数 KMBL
    uint32_t magic{kMmapSlotMagic};
    /// @brief 版本号
    uint16_t version{kMmapSlotVersion};
    /// @brief 头部长度
    uint16_t header_size{sizeof(MmapSlotHeader)};
    /// @brief mmap slot第几轮次
    uint64_t slot_epoch{0};
    /// @brief 日志文件轮次
    uint64_t log_generation{0};
    /// @brief 当前日志数据内容批次
    uint64_t log_batch_sequence{0};
    /// @brief mmap当前写入的有效长度
    uint64_t payload_size{0};
    /// @brief mmap全部有效数据的crc
    uint32_t payload_crc32{0};
    /// @brief 当前mmap所处状态
    uint32_t state{static_cast<uint32_t>(MmapSlotState::kEmpty)};

};


class MmapSlot: Noncopyable
{
public:
    ~MmapSlot();
    LogBackendResult open(const std::string& path, size_t payload_capacity);

    void close() noexcept;
    bool writable() const noexcept;
    bool append(Span<const uint8_t> record) noexcept;
    bool begin(uint64_t generation, uint64_t sequence) noexcept;
    bool seal() noexcept;
    bool markProcessing() noexcept;
    bool reset(uint64_t next_epoch) noexcept;
    Span<uint8_t> payload() const noexcept;

    MmapSlotHeader& header() const noexcept { return *header_; }
    MmapSlotHeader& header() noexcept { return *header_; }

    uint64_t payloadCapacity() const noexcept { return payload_capacity_; }
    int fd() const noexcept { return fd_; }
    uint64_t remainedSize() const noexcept  { return payload_capacity_ - header_->payload_size; }

private:
    bool syncHeader(int flags) noexcept;

private:
    /// @brief 映射文件绝对路径
    std::string file_path_;
    /// @brief 文件对应fd
    int32_t fd_{-1};
    /// @brief 文件映射后的mmap句柄
    void *mmap_handle_{nullptr};
    /// @brief 映射使用的内存空间大小(不可变)
    uint64_t mmap_size_{0};
    /// @brief 业务负载容量上限
    uint64_t payload_capacity_{0};
    /// @brief 内存空间上slot头偏移位置
    MmapSlotHeader* header_{nullptr};
    /// @brief 内存空间上业务负载偏移位置
    uint8_t* payload_{nullptr};
};

struct MmapSlotPaths
{
    std::array<std::string, 2> path;

    static MmapSlotPaths MakePaths(const std::string& active_path)
    {
        // e.g. net.slot0.mmap
        return {
            {active_path + ".slot0.mmap", active_path + ".slot1.mmap"}
        };
    }

};

enum class MmapBackendRotateStage
{
    kSealPending,
    kSealed,
    kBatchesDrained,
    kActiveClosed,
    kActiveRenamed,
    kRotateFinished,
};

enum class MmapArchiveDisposition
{
    kRawOnly,
    kPending,
    kRealtimePublished,
    kCompensationQueued,
    kRawRetained,
};

struct MmapRotateContext
{
    LogFileRotateRequest request;
    std::string archive_compression_path;
    uint64_t final_sequence{0};
    bool archive_committed{false};
    std::shared_ptr<MmapBatchResult> end_result;

    /// @brief  已尝试投递或决定跳过；active 重试不得再次提交。
    bool end_decided{false};
    /// @brief 是否进行压缩补偿  TODO 考虑去除 压缩补偿在最外层
    bool is_compress_compensation{false};
    /// @brief 归档是否处理
    bool archive_handled{false};
    /// @brief 本代次日志是否重试
    bool generation_retired{false};

    MmapArchiveDisposition disposition{MmapArchiveDisposition::kRawOnly};
    std::error_code compression_error;
    std::error_code cleanup_error;
};
static_assert(std::is_nothrow_move_constructible<MmapRotateContext>::value,
    "archive handoff must not throw after active commit");
static_assert(std::is_nothrow_move_assignable<MmapRotateContext>::value,
    "archive collection must not throw while erasing");

struct MmapRotationReport
{
    uint64_t generation{0};
    MmapArchiveDisposition disposition{MmapArchiveDisposition::kRawOnly};
    std::error_code compression_error;
    std::error_code cleanup_error;
};

struct RecoveredSlot
{
    size_t index{0};
    uint64_t generation{0};
    uint64_t sequence{0};
};



struct MmapBackendOptions
{
    size_t slot_payload_capacity{0};
    size_t batch_submit_bytes{0};
    bool compression_enabled{true};
    std::chrono::milliseconds flush_timeout{30000};
};

class MmapBufferedLogFileBackend final : public LogFileBackend
{
public:
    MmapBufferedLogFileBackend(MmapBatchScheduler& mmap_scheduler, FullRecompressScheduler& recompress_scheduler, MmapBackendOptions options);
    ~MmapBufferedLogFileBackend() override;

    LogBackendResult open(const std::string& normalize_path) override;
    void close() noexcept override;
    LogBackendResult append(std::string_view bytes) override;
    LogBackendResult flush() noexcept override;
    LogBackendResult rotate(const LogFileRotateRequest& request, int64_t timeout_ms = -1) noexcept override;

    bool isOpen() const noexcept override { return is_open_; }
    uint64_t openSize() const noexcept override { return active_open_size_; }
    uint64_t generation() const noexcept override { return generation_; }
    uint64_t lastSequence() const noexcept override { return last_sequence_; }

    bool recompress(std::string archive_path) noexcept override;

    void pollArchiveCompletions() noexcept override;

    bool archiveInUse(const std::string& path) const noexcept override;

    bool hasPendingArchives() const noexcept override;

    const MmapRotationReport& lastRotationReport() const noexcept { return last_rotation_report_; }




private:
    LogBackendResult flushFor(int64_t timeout_ms) noexcept;
    LogBackendResult openSlots(const std::string& active_path);
    LogBackendResult sealWritable();
    bool acquireWritable(int64_t timeout_ms);
    LogBackendResult reclaimCompleted();

    /*
        以下3个函数都是日志恢复时使用
    */

    /**
     * @brief 读出磁盘上mmap文件头部信息, 重建内存对象
     * @param committed 
     * @return LogBackendResult 
     */
    LogBackendResult inspectSlots(std::vector<RecoveredSlot>& committed);
    /**
     * @brief 根据现有的残留信息+数据，直接完成一次sealedbatch 投递
     * @param committed 
     * @return LogBackendResult 
     */
    LogBackendResult replayInspected(const std::vector<RecoveredSlot>& committed);
    /**
     * @brief 恢复时直接将active文件内容重新写入一次压缩文件
     * @return std::error_code 
     */
    std::error_code importExistingActive() noexcept;

    /**
     * @brief (暂时弃用)全量压缩补偿调度(目标路径下没有目标归档时触发)
     * @param context 
     * @return true 
     * @return false 
     */
    bool scheduleRotationCompensation(const MmapRotateContext& context) noexcept;

    void submitRotationEnd(MmapRotateContext& context) noexcept;
    bool finishRotationArchive(MmapRotateContext& context) noexcept;
    void recordRotationReport(const MmapRotateContext& context) noexcept;

public:
    // 特别注意 容量选取和刷新阈值有关, 而且mmap的flush操作比普通写文件要复杂
    static constexpr size_t kDefaultSlotPayloadCapacity = 12*1024*1024; // 12M

private:
    /// @brief mmap双写调度器
    MmapBatchScheduler& mmap_scheduler_;
    /// @brief 全量压缩补偿调度器
    FullRecompressScheduler& compress_scheduler_;
    /// @brief 压缩文件后缀
    const std::string compression_codec_suffix_;
    /// @brief 初始化以及部分配置参数
    MmapBackendOptions options_;
    /// @brief mmap 容器
    std::array<std::unique_ptr<MmapSlot>, 2> slots_;

    /// @brief 表示写完成但还没提交到对应工作线程中的日志数据批次
    std::array<std::shared_ptr<MmapSealedBatch>, 2> in_flight_{nullptr, nullptr};

    /// @brief mmap批次写入目的地
    std::shared_ptr<MmapBatchTarget> target_;
    /// @brief 日志轮转上下文
    // std::optional<MmapRotateContext> rotate_ctx_;
    // 只收取旧代归档结果，不再次执行 active 轮转状态机。
    // 首次固定请求前预留一个元素，保证 rename 后移交不分配内存。
    std::vector<MmapRotateContext> pending_archives_;
    /// @brief 最后一次轮转状态报告
    MmapRotationReport last_rotation_report_;
    // seal/begin 同步失败后禁止继续接收新数据，保留 slot 给恢复流程。
    bool slot_io_failed_{false};
    // 普通 open 只重开 fd，不重建 generation/session。
    bool active_reopen_pending_{false}; 

    bool is_open_{false};
    /// @brief 当前正在写入的slot下标
    size_t writable_index_{0};
    /// @brief mmap 容器代次
    uint64_t next_epoch_{1};
    /// @brief active文件打开时的大小
    uint64_t active_open_size_{0};
};


}
#endif //__KIT_MMAP_LOG_FILE_BACKEND_H__