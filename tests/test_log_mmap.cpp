#include "base/log_mmap_completion.h"
#include "base/log_full_recompress.h"
#include "base/log_config.h"
#include "base/log_file_sink.h"
#include "base/log_compress_coordinator.h"
#include "log_test_support.h"

#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

using namespace kit_muduo;
using namespace log_test;
using namespace std::chrono_literals;

namespace {

Span<const uint8_t> Bytes(std::string_view text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

class RecordingRecompressScheduler final : public FullRecompressScheduler
{
public:
    bool submit(FullRecompressTask task) noexcept override
    {
        if (accept)
        {
            in_use_paths.push_back(task.archive_log_path);
            in_use_paths.push_back(task.archive_compression_path);
        }
        tasks.push_back(std::move(task));
        return accept;
    }
    std::string compressSuffix() const noexcept override { return "zst"; }
    bool archiveInUse(const std::string& path) const noexcept override
    {
        for (const auto& in_use_path : in_use_paths)
            if (path == in_use_path) return true;
        return false;
    }
    bool accept{false};
    std::vector<FullRecompressTask> tasks;
    std::vector<std::string> in_use_paths;
};

// 仅调度可控；batch 读写、压缩、rename、结果发布使用真实 target。
// 不启动隐式后台任务，End 必须由 finishEnds 显式终结，避免 fixture 析构悬挂。
class CapturingScheduler final : public MmapBatchScheduler
{
public:
    static void CompleteActive(const MmapSealedBatch::Ptr& batch, std::error_code error = {})
    {
        if (!batch->completion().startActive()) return;
        if (!error) error = batch->target().appendActive(batch->generation(), batch->sequence(), batch->payload());
        if (error) batch->target().setActiveFailed();
        batch->completion().completeActive(error);
        batch->target().activeCompleted(batch->generation(), batch->sequence());
    }
    static void CompleteCompression(const MmapSealedBatch::Ptr& batch, std::error_code error = {})
    {
        if (!batch->compressionEnabled())
        {
            if (batch->completion().skipCompression("disabled"))
                batch->target().compressionCompleted(batch->generation(), batch->sequence());
            return;
        }
        if (!batch->completion().startCompression()) return;
        if (!error) error = batch->target().continueCompression(batch->generation(), batch->sequence(), batch->payload());
        if (error) batch->target().setDegraded(batch->generation(), "injected compression error");
        batch->completion().completeCompression(error);
        batch->target().compressionCompleted(batch->generation(), batch->sequence());
    }
    bool submitBatch(MmapSealedBatch::Ptr batch) noexcept override
    {
        attempts.push_back(batch);
        if (!accept_batch) return false;
        batches.push_back(batch);
        batch->target().submitted(batch->generation(), batch->sequence());
        if (auto_active) CompleteActive(batch, active_error);
        if (auto_compression) CompleteCompression(batch, compression_error);
        return true;
    }
    bool submitBatchEnd(MmapBatchEndTask task) noexcept override
    {
        ++end_attempts;
        if (!accept_end) return false;
        ends.push_back(std::move(task));
        if (auto_end) finishEnds();
        return true;
    }
    bool submitActiveRotate(MmapActiveRotateTask task) noexcept override
    {
        ++rotate_attempts;
        if (!accept_rotate) return false;
        rotate_paths.push_back(task.archive_log_path);
        if (defer_rotate)
        {
            rotate_worker = std::thread([this, task = std::move(task)] {
                rotate_gate.enterAndWait();
                rotate_thread = std::this_thread::get_id();
                task.result->complete(task.target->rotateActive(task.archive_log_path, task.deadline));
            });
            return true;
        }
        auto outcome = task.target->rotateActive(task.archive_log_path, task.deadline);
        task.result->complete(outcome);
        return true;
    }
    void finishEnds(std::error_code injected = {})
    {
        for (const auto& task : ends)
        {
            std::error_code old;
            if (!task.result->waitFor(0, old))
                task.result->complete(injected ? injected : task.target->finishCompression(task.generation));
        }
    }
    void finishBatches()
    {
        for (const auto& batch : batches)
        {
            CompleteActive(batch);
            CompleteCompression(batch);
        }
    }
    bool accept_batch{true}, accept_rotate{true}, accept_end{true};
    bool auto_active{true}, auto_compression{true}, auto_end{true};
    bool defer_rotate{false};
    Gate rotate_gate;
    std::thread rotate_worker;
    std::thread::id rotate_thread;
    std::error_code active_error, compression_error;
    size_t rotate_attempts{0}, end_attempts{0};
    std::vector<MmapSealedBatch::Ptr> attempts, batches;
    std::vector<MmapBatchEndTask> ends;
    std::vector<std::string> rotate_paths;
};

class MmapBackendTest : public testing::Test
{
protected:
    void SetUp() override
    {
        active_ = directory_.file("active.log");
        archive_ = directory_.file("archive.log");
        options_.slot_payload_capacity = 64;
        options_.batch_submit_bytes = 8;
        options_.flush_timeout = 20ms;
        backend_ = std::make_unique<MmapBufferedLogFileBackend>(scheduler_, full_, options_);
        ASSERT_TRUE(backend_->open(active_).ok());
    }
    void TearDown() override
    {
        // 即使 ASSERT 提前返回，也先终结已接收任务，再 flush 尾部并释放 mmap。
        scheduler_.rotate_gate.release();
        if (scheduler_.rotate_worker.joinable()) scheduler_.rotate_worker.join();
        scheduler_.accept_batch = true;
        scheduler_.auto_active = scheduler_.auto_compression = true;
        scheduler_.finishBatches();
        if (backend_->isOpen()) (void)backend_->flush();
        scheduler_.finishBatches();
        scheduler_.finishEnds();
        backend_->pollArchiveCompletions();
        backend_->close();
        backend_.reset();
    }
    LogFileRotateRequest request() const
    {
        LogFileRotateRequest request;
        request.generation = backend_->generation();
        request.last_sequence = backend_->lastSequence();
        request.active_path = active_;
        request.archive_log_path = archive_;
        request.compression_enabled = options_.compression_enabled;
        return request;
    }
    TempDirectory directory_;
    CapturingScheduler scheduler_;
    RecordingRecompressScheduler full_;
    MmapBackendOptions options_;
    std::unique_ptr<MmapBufferedLogFileBackend> backend_;
    std::string active_, archive_;
};

// 使用真实 sink/backend 验证扫描时机，仅控制后台任务的受理与完成。
class MmapSinkCompensationTest : public testing::Test
{
protected:
    void SetUp() override
    {
        active_ = directory_.file("active.log");
        config_.backend = LogFileBackendType::kMmapBuffered;
        config_.compress_rotated = true;
        config_.rotate_max_bytes = 8;
        config_.flush_threshold = 8;
        config_.rotate_max_backup_files = 10;
        registry_.setConfig(config_);
        auto backend = std::make_unique<MmapBufferedLogFileBackend>(
            scheduler_, full_, MmapBackendOptions{64, 8, true, 1000ms});
        backend_ = backend.get();
        sink_ = std::make_unique<LogFileSink>(&registry_, active_, std::move(backend));
        ASSERT_TRUE(sink_->ensureOpen().ok());
    }
    void TearDown() override
    {
        // ASSERT 提前返回也要结清已接受任务，避免带着 pending End 释放 backend。
        scheduler_.rotate_gate.release();
        if (scheduler_.rotate_worker.joinable()) scheduler_.rotate_worker.join();
        scheduler_.accept_batch = true;
        scheduler_.auto_active = scheduler_.auto_compression = true;
        scheduler_.finishBatches();
        if (backend_->isOpen()) (void)sink_->flush();
        scheduler_.finishBatches();
        scheduler_.finishEnds();
        EXPECT_TRUE(sink_->finishArchiveCompletions().ok());
        EXPECT_TRUE(sink_->closeForShutdown().ok());
        sink_.reset();
    }

    TempDirectory directory_;
    CapturingScheduler scheduler_;
    RecordingRecompressScheduler full_;
    LogFileConfig config_;
    LogFileSinkRegister registry_;
    MmapBufferedLogFileBackend* backend_{nullptr};
    std::unique_ptr<LogFileSink> sink_;
    std::string active_;
};

void SeedSlot(const std::string& active, size_t index, uint64_t generation,
    uint64_t sequence, std::string_view payload, MmapSlotState state)
{
    MmapSlot slot;
    const auto path = MmapSlotPaths::MakePaths(active).path[index];
    if (!slot.open(path, 64).ok() || !slot.begin(generation, sequence)
        || !slot.append(Bytes(payload))) throw std::runtime_error("cannot seed recovery slot");
    if (state == MmapSlotState::kWritable) return;
    if (!slot.seal()) throw std::runtime_error("cannot seal recovery slot");
    if (state == MmapSlotState::kProcessing && !slot.markProcessing())
        throw std::runtime_error("cannot mark recovery slot processing");
}

} // namespace

// 测试思路：只有 active 成功且两分支终态才可回收；例：压缩失败/active 成功=true，反之=false。
TEST(LogMmapCompletionTest, FourOutcomesPreserveActiveAuthority)
{
    for (bool active_failed : {false, true})
        for (bool compression_failed : {false, true})
        {
            SCOPED_TRACE(testing::Message() << active_failed << "/" << compression_failed);
            LogMmapBatchCompletion completion;
            EXPECT_FALSE(completion.completeActive());
            EXPECT_FALSE(completion.completeCompression());
            ASSERT_TRUE(completion.startActive());
            ASSERT_TRUE(completion.startCompression());
            EXPECT_FALSE(completion.startActive());
            const auto error = std::make_error_code(std::errc::io_error);
            ASSERT_TRUE(completion.completeCompression(compression_failed ? error : std::error_code{}, "compression"));
            EXPECT_FALSE(completion.canReleaseSlot());
            ASSERT_TRUE(completion.completeActive(active_failed ? error : std::error_code{}, "active"));
            EXPECT_TRUE(completion.bothTerminal());
            EXPECT_EQ(completion.canReleaseSlot(), !active_failed);
            EXPECT_EQ(completion.activeResult().error, active_failed ? error : std::error_code{});
            EXPECT_EQ(completion.compressionResult().message, "compression");
            EXPECT_FALSE(completion.completeActive());
            EXPECT_FALSE(completion.completeCompression());
        }
}

// 测试思路：关闭压缩使用 Skipped 终态，不能先于 active 回收；例：skip -> active success -> release。
TEST(LogMmapCompletionTest, SkippedCompressionStillWaitsForActive)
{
    LogMmapBatchCompletion completion;
    ASSERT_TRUE(completion.skipCompression("disabled"));
    EXPECT_FALSE(completion.skipCompression());
    EXPECT_FALSE(completion.startCompression());
    EXPECT_FALSE(completion.canReleaseSlot());
    ASSERT_TRUE(completion.startActive());
    ASSERT_TRUE(completion.completeActive());
    EXPECT_TRUE(completion.canReleaseSlot());
    EXPECT_EQ(completion.compressionState(), LogMmapBranchState::kSkipped);
    EXPECT_EQ(completion.compressionResult().message, "disabled");
}

// 测试思路：并发完成两个不同分支时，终态和错误消息必须一起可见；例：active=ENOSPC、compression=OK。
TEST(LogMmapCompletionTest, ConcurrentBranchesPublishResults)
{
    LogMmapBatchCompletion completion;
    const auto error = std::make_error_code(std::errc::no_space_on_device);
    std::thread active([&completion, error] {
        EXPECT_TRUE(completion.startActive());
        EXPECT_TRUE(completion.completeActive(error, "disk full"));
    });
    std::thread compressed([&completion] {
        EXPECT_TRUE(completion.startCompression());
        EXPECT_TRUE(completion.completeCompression());
    });
    active.join();
    compressed.join();
    EXPECT_TRUE(completion.bothTerminal());
    EXPECT_FALSE(completion.canReleaseSlot());
    EXPECT_EQ(completion.activeResult().error, error);
    EXPECT_EQ(completion.activeResult().message, "disk full");
}

// 测试思路：小 slot 只能接收完整记录，seal 后禁止写；例：8B 容量装入 5B 后再写 4B 被拒绝。
TEST(MmapSlotTest, CommitResetAndRecordBoundary)
{
    TempDirectory directory;
    MmapSlot slot;
    ASSERT_TRUE(slot.open(directory.file("slot.mmap"), 8).ok());
    ASSERT_TRUE(slot.begin(7, 1));
    ASSERT_TRUE(slot.append(Bytes("hello")));
    EXPECT_FALSE(slot.append(Bytes("oops")));
    EXPECT_EQ(slot.header().payload_size, 5U);
    ASSERT_TRUE(slot.seal());
    EXPECT_EQ(static_cast<MmapSlotState>(slot.header().state), MmapSlotState::kSealed);
    EXPECT_FALSE(slot.append(Bytes("!")));
    ASSERT_TRUE(slot.markProcessing());
    EXPECT_FALSE(slot.writable());
    ASSERT_TRUE(slot.reset(9));
    EXPECT_EQ(slot.header().slot_epoch, 9U);
    EXPECT_EQ(slot.header().payload_size, 0U);
    EXPECT_EQ(static_cast<MmapSlotState>(slot.header().state), MmapSlotState::kEmpty);
}

// 测试思路：sealed batch 只持有映射视图，Processing 仅允许恢复入口；例：payload 地址完全相同。
TEST(MmapSlotTest, BatchSharesMappingAndRestrictsPublicationState)
{
    TempDirectory directory;
    MmapSlot slot;
    auto target = std::make_shared<MmapBatchTarget>(directory.file("active.log"));
    ASSERT_TRUE(slot.open(directory.file("slot.mmap"), 64).ok());
    ASSERT_TRUE(slot.begin(7, 3));
    ASSERT_TRUE(slot.append(Bytes("line\n")));
    EXPECT_THROW(MmapSealedBatch::Make(slot, target, true), std::logic_error);
    ASSERT_TRUE(slot.seal());
    auto batch = MmapSealedBatch::Make(slot, target, true);
    EXPECT_EQ(batch->payload().data(), slot.payload().data());
    EXPECT_EQ(batch->payload().size(), 5U);
    EXPECT_EQ(batch->generation(), 7U);
    EXPECT_EQ(batch->sequence(), 3U);
    ASSERT_TRUE(slot.markProcessing());
    EXPECT_THROW(MmapSealedBatch::Make(slot, target, true), std::logic_error);
    EXPECT_NO_THROW(MmapSealedBatch::Make(slot, target, true, true));
    EXPECT_THROW(MmapSealedBatch::Make(slot, nullptr, true, true), std::invalid_argument);
}

// 测试思路：完成通知 1、3 不可跳过 2；例：barrier(3)=false，补 2 后再通知 3 才成功。
TEST(MmapTargetTest, ProgressNeverCrossesSequenceGap)
{
    TempDirectory directory;
    MmapBatchTarget target(directory.file("active.log"));
    target.beginGeneration(7);
    for (uint64_t seq = 1; seq <= 3; ++seq) target.submitted(7, seq);
    target.activeCompleted(7, 1);
    target.activeCompleted(7, 3);
    target.compressionCompleted(7, 1);
    target.compressionCompleted(7, 3);
    EXPECT_EQ(target.progress(7).active_completed_sequence, 1U);
    EXPECT_FALSE(target.waitProgress(7, 3, true, 0));
    target.activeCompleted(7, 2);
    target.activeCompleted(7, 3);
    EXPECT_TRUE(target.waitProgress(7, 3, false, 0));
    EXPECT_FALSE(target.waitProgress(7, 3, true, 0));
    target.compressionCompleted(7, 2);
    target.compressionCompleted(7, 3);
    EXPECT_TRUE(target.waitProgress(7, 3, true, 0));
    EXPECT_FALSE(target.waitProgress(99, 0, false, 0));
}

// 测试思路：尾部未达 seal 阈值时 request.sequence=0；rotate 必须 seal 为 1 后再归档。
// 例："tail\n" 全部在旧压缩归档，新 active 为空，新代 sequence=0。
TEST_F(MmapBackendTest, RotationSealsTailAndPublishesExactContent)
{
    ASSERT_TRUE(backend_->append("tail\n").ok());
    EXPECT_EQ(backend_->lastSequence(), 0U);
    const auto generation = backend_->generation();
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(backend_->generation(), generation + 1);
    EXPECT_EQ(backend_->lastSequence(), 0U);
    ASSERT_EQ(scheduler_.ends.size(), 1U);
    EXPECT_EQ(scheduler_.ends[0].last_sequence, 1U);
    EXPECT_EQ(DecompressFile(archive_ + ".zst"), "tail\n");
    EXPECT_FALSE(std::filesystem::exists(archive_));
    EXPECT_EQ(ReadFile(active_), "");
}

// 测试思路：batch 入队拒绝时保留已接受记录，不推进 sequence；恢复后必须重投同一个 batch。
// 例：8B append 成功、flush 失败；开放调度后旧 8B 恰好写入一次，再写 new。
TEST_F(MmapBackendTest, RejectedBatchRetriesWithoutDuplicatingAcceptedRecord)
{
    scheduler_.accept_batch = false;
    ASSERT_TRUE(backend_->append("12345678").ok());
    EXPECT_EQ(backend_->lastSequence(), 0U);
    EXPECT_FALSE(backend_->flush().ok());
    EXPECT_FALSE(backend_->rotate(request(), 20).ok());
    EXPECT_EQ(scheduler_.rotate_attempts, 0U);
    ASSERT_FALSE(scheduler_.attempts.empty());
    const auto* first = scheduler_.attempts.front().get();
    for (const auto& batch : scheduler_.attempts) EXPECT_EQ(batch.get(), first);
    scheduler_.accept_batch = true;
    ASSERT_TRUE(backend_->flush().ok());
    ASSERT_TRUE(backend_->append("new").ok());
    ASSERT_TRUE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "12345678new");
    EXPECT_EQ(backend_->lastSequence(), 2U);
}

// 测试思路：压缩失败为终态，active 成功仍允许 flush 与 slot 复用，但不启动全量补偿。
// 例：8B batch 压缩 EIO，flush 成功；后续再写 8B，active 精确包含两批，无重放。
TEST_F(MmapBackendTest, FlushAcceptsCompressionDegradationAndReusesSlots)
{
    scheduler_.compression_error = std::make_error_code(std::errc::io_error);
    ASSERT_TRUE(backend_->append("12345678").ok());
    ASSERT_TRUE(backend_->flush().ok());
    ASSERT_EQ(scheduler_.batches.size(), 1U);
    EXPECT_TRUE(scheduler_.batches[0]->completion().bothTerminal());
    EXPECT_TRUE(scheduler_.batches[0]->completion().compressionResult().error);
    ASSERT_TRUE(backend_->append("abcdefgh").ok());
    EXPECT_TRUE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "12345678abcdefgh");
    EXPECT_TRUE(full_.tasks.empty());
}

// 测试思路：active 成功而 compression 未结束时 flush 超时，两块 slot 仍不能复用。
// 例：A/B 占满两 slot 后 C 拒绝；完成 compression 后 C 可写且内容为 A+B+C。
TEST_F(MmapBackendTest, FlushWaitsForBothBranchesBeforeSyncAndReuse)
{
    scheduler_.auto_compression = false;
    ASSERT_TRUE(backend_->append("AAAAAAAA").ok());
    ASSERT_TRUE(backend_->append("BBBBBBBB").ok());
    EXPECT_FALSE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "AAAAAAAABBBBBBBB");
    EXPECT_FALSE(backend_->append("C").ok());
    ASSERT_EQ(scheduler_.batches.size(), 2U);
    for (const auto& batch : scheduler_.batches) EXPECT_FALSE(batch->completion().canReleaseSlot());
    scheduler_.finishBatches();
    scheduler_.auto_compression = true;
    ASSERT_TRUE(backend_->append("C").ok());
    ASSERT_TRUE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "AAAAAAAABBBBBBBBC");
}

// 测试思路：active 控制任务拒绝不能 rename/End，也不推进代次；例：再次请求才真正归档。
TEST_F(MmapBackendTest, RejectedActiveRotationLeavesGenerationWritable)
{
    ASSERT_TRUE(backend_->append("before").ok());
    const auto generation = backend_->generation();
    scheduler_.accept_rotate = false;
    EXPECT_FALSE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(backend_->generation(), generation);
    EXPECT_EQ(scheduler_.end_attempts, 0U);
    EXPECT_FALSE(std::filesystem::exists(archive_));
    ASSERT_TRUE(backend_->append("after").ok());
    scheduler_.accept_rotate = true;
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(DecompressFile(archive_ + ".zst"), "beforeafter");
}

// 测试思路：active 控制任务受理不等于物理完成；例：后台门闩未释放时 rotate future 仍等待。
// 释放后在独立 worker 完成 rename/reopen，调用者才收到成功，归档包含完整 tail。
TEST_F(MmapBackendTest, AcceptedRotationWaitsForPhysicalWorkerResult)
{
    scheduler_.defer_rotate = true;
    ASSERT_TRUE(backend_->append("tail").ok());
    const auto rotate_request = request();
    auto rotating = std::async(std::launch::async, [this, rotate_request] {
        return backend_->rotate(rotate_request, -1);
    });
    ReleaseGateOnExit release(scheduler_.rotate_gate);
    ASSERT_TRUE(scheduler_.rotate_gate.waitForEntered());
    EXPECT_EQ(rotating.wait_for(20ms), std::future_status::timeout);
    EXPECT_FALSE(std::filesystem::exists(archive_));
    scheduler_.rotate_gate.release();
    EXPECT_TRUE(rotating.get().ok());
    scheduler_.rotate_worker.join();
    EXPECT_NE(scheduler_.rotate_thread, std::this_thread::get_id());
    EXPECT_EQ(DecompressFile(archive_ + ".zst"), "tail");
}

// 测试思路：已过 deadline 的控制任务不得 close/rename；例：旧文件仍能追加 "after"。
TEST(MmapTargetTest, ExpiredRotationDoesNotChangeActiveFile)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    const auto archive = directory.file("archive.log");
    MmapBatchTarget target(active);
    ASSERT_TRUE(target.openActive().ok());
    ASSERT_FALSE(target.appendActive(1, 1, Bytes("before")));
    const auto outcome = target.rotateActive(archive, std::chrono::steady_clock::now() - 1s);
    EXPECT_EQ(outcome.error, std::errc::timed_out);
    EXPECT_FALSE(outcome.archive_committed);
    EXPECT_TRUE(outcome.active_open);
    EXPECT_FALSE(std::filesystem::exists(archive));
    ASSERT_FALSE(target.appendActive(1, 2, Bytes("after")));
    EXPECT_EQ(ReadFile(active), "beforeafter");
}

// 测试思路：非法轮转请求在任何 I/O 前拒绝；例：错代次、错 sequence、错 active、同路径均不 seal。
TEST_F(MmapBackendTest, InvalidRotationRequestDoesNotSubmitControlTask)
{
    ASSERT_TRUE(backend_->append("tail").ok());
    for (int field = 0; field < 5; ++field)
    {
        auto invalid = request();
        if (field == 0) ++invalid.generation;
        if (field == 1) ++invalid.last_sequence;
        if (field == 2) invalid.active_path += ".wrong";
        if (field == 3) invalid.archive_log_path = active_;
        if (field == 4) invalid.compression_enabled = false;
        EXPECT_FALSE(backend_->rotate(invalid, 1000).ok());
    }
    EXPECT_EQ(scheduler_.rotate_attempts, 0U);
    EXPECT_TRUE(scheduler_.batches.empty());
}

// 测试思路：归档重名必须无覆盖失败；失败后继续同代压缩，再用新归档名轮转。
// 例：已有 archive="keep" 不变，新归档解压为 "firstsecond"，End 仅成功轮转时一次。
TEST_F(MmapBackendTest, RenameConflictKeepsOldStreamUsable)
{
    WriteFile(archive_, "keep");
    ASSERT_TRUE(backend_->append("first").ok());
    const auto generation = backend_->generation();
    EXPECT_FALSE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(backend_->generation(), generation);
    EXPECT_EQ(ReadFile(archive_), "keep");
    EXPECT_EQ(scheduler_.end_attempts, 0U);
    ASSERT_TRUE(backend_->append("second").ok());
    archive_ = directory_.file("second.log");
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(DecompressFile(archive_ + ".zst"), "firstsecond");
    EXPECT_EQ(scheduler_.end_attempts, 1U);
}

// 测试思路：End 尚未完成不阻塞已完成的 active 轮转；例：旧归档受保护，新代仍写入 new。
TEST_F(MmapBackendTest, DelayedEndProtectsArchiveAndDoesNotBlockNewGeneration)
{
    scheduler_.auto_end = false;
    ASSERT_TRUE(backend_->append("old").ok());
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_TRUE(backend_->hasPendingArchives());
    EXPECT_TRUE(backend_->archiveInUse(archive_));
    EXPECT_TRUE(backend_->archiveInUse(archive_ + ".zst"));
    EXPECT_EQ(ReadFile(archive_), "old");
    ASSERT_TRUE(backend_->append("new").ok());
    ASSERT_TRUE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "new");
    scheduler_.finishEnds();
    backend_->pollArchiveCompletions();
    backend_->pollArchiveCompletions();
    EXPECT_FALSE(backend_->hasPendingArchives());
    EXPECT_FALSE(backend_->archiveInUse(archive_));
    EXPECT_EQ(scheduler_.end_attempts, 1U);
    EXPECT_EQ(DecompressFile(archive_ + ".zst"), "old");
}

// 测试思路：旧代 End 晚失败后不调用发布覆盖原始错误，也不在 backend 内立即补偿。
// 例：旧 End=EIO，新 active="new!"；旧 raw="old"、报告仍为 EIO，重复 poll 后补偿次数=0。
TEST_F(MmapBackendTest, LateEndFailureRetainsOriginalErrorAndRawWithoutCompensation)
{
    scheduler_.auto_end = false;
    full_.accept = true;
    const auto generation = backend_->generation();
    ASSERT_TRUE(backend_->append("old").ok());
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    ASSERT_TRUE(backend_->append("new").ok());
    ASSERT_TRUE(backend_->flush().ok());
    scheduler_.finishEnds(std::make_error_code(std::errc::io_error));
    backend_->pollArchiveCompletions();
    backend_->pollArchiveCompletions();
    EXPECT_TRUE(std::filesystem::exists(archive_)) << "failed End must retain raw text";
    EXPECT_FALSE(std::filesystem::exists(archive_ + ".zst"));
    EXPECT_EQ(backend_->lastRotationReport().compression_error, std::errc::io_error);
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(backend_->lastRotationReport().generation, generation);
    EXPECT_EQ(backend_->lastRotationReport().disposition, MmapArchiveDisposition::kRawRetained);
    EXPECT_FALSE(backend_->hasPendingArchives());
    EXPECT_FALSE(backend_->archiveInUse(archive_));
    ASSERT_TRUE(backend_->append("!").ok());
    ASSERT_TRUE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "new!");
    EXPECT_EQ(ReadFile(archive_), "old");
}

// 测试思路：End 拒绝时 raw 保留，backend 不发布或立即补偿，等待 sink 后续扫描。
// 例：报告 EAGAIN/kRawRetained，active 轮转成功，重复 poll 也不会提交 full 任务。
TEST_F(MmapBackendTest, EndRejectionRetainsRawArchiveForLaterCompensation)
{
    scheduler_.accept_end = false;
    ASSERT_TRUE(backend_->append("raw").ok());
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(scheduler_.end_attempts, 1U);
    EXPECT_TRUE(std::filesystem::exists(archive_)) << "rejected End must retain raw text";
    EXPECT_FALSE(std::filesystem::exists(archive_ + ".zst"));
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(backend_->lastRotationReport().compression_error, std::errc::resource_unavailable_try_again);
    EXPECT_EQ(backend_->lastRotationReport().disposition, MmapArchiveDisposition::kRawRetained);
    EXPECT_EQ(ReadFile(archive_), "raw");
    EXPECT_FALSE(backend_->hasPendingArchives());
    backend_->pollArchiveCompletions();
    EXPECT_TRUE(full_.tasks.empty());
}

// 测试思路：compression batch 失败不能阻断 active 归档，也不能发布不完整压缩流。
// 例：压缩 EIO 后跳过 End，报告 EIO/kRawRetained，原文保留且 backend 不立即补偿。
TEST_F(MmapBackendTest, CompressionFailureDoesNotFailActiveRotation)
{
    scheduler_.compression_error = std::make_error_code(std::errc::io_error);
    ASSERT_TRUE(backend_->append("12345678").ok());
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(scheduler_.end_attempts, 0U);
    EXPECT_TRUE(std::filesystem::exists(archive_)) << "failed compression must retain raw text";
    EXPECT_FALSE(std::filesystem::exists(archive_ + ".zst"));
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(backend_->lastRotationReport().compression_error, std::errc::io_error);
    EXPECT_EQ(backend_->lastRotationReport().disposition, MmapArchiveDisposition::kRawRetained);
    backend_->pollArchiveCompletions();
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(ReadFile(archive_), "12345678");
    EXPECT_TRUE(backend_->isOpen());
}

// 测试思路：End 晚失败只保留 raw；下一次 sink 成功轮转才扫描并提交该旧归档。
// 例：A --rotate--> B --End(A)=EIO--> C --rotate--> D；A 只补偿一次，B/C 正常实时发布。
//       End 终态/poll          下一次 rotate          再下一次 rotate
//       A raw，full=0     ->   submit(A)，full=1   ->   A 在途，full=1
TEST_F(MmapSinkCompensationTest, LaterRotationCompensatesFailedArchiveOnce)
{
    scheduler_.auto_end = false;
    full_.accept = true;
    ASSERT_TRUE(sink_->append("aaaaaaaa").ok());
    ASSERT_TRUE(sink_->append("bbbbbbbb").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 1U);
    const auto old_archive = scheduler_.rotate_paths.front();
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_TRUE(backend_->archiveInUse(old_archive));

    scheduler_.finishEnds(std::make_error_code(std::errc::io_error));
    ASSERT_TRUE(sink_->finishArchiveCompletions().ok());
    EXPECT_EQ(backend_->lastRotationReport().compression_error, std::errc::io_error);
    EXPECT_EQ(backend_->lastRotationReport().disposition, MmapArchiveDisposition::kRawRetained);
    EXPECT_FALSE(backend_->archiveInUse(old_archive));
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");
    EXPECT_EQ(ReadFile(active_), "bbbbbbbb");

    scheduler_.auto_end = true;
    ASSERT_TRUE(sink_->append("cccccccc").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 2U);
    ASSERT_EQ(full_.tasks.size(), 1U);
    EXPECT_EQ(full_.tasks.front().archive_log_path, old_archive);
    EXPECT_EQ(full_.tasks.front().archive_compression_path, old_archive + ".zst");
    EXPECT_TRUE(backend_->archiveInUse(old_archive));
    EXPECT_TRUE(backend_->archiveInUse(old_archive + ".zst"));
    EXPECT_EQ(DecompressFile(scheduler_.rotate_paths[1] + ".zst"), "bbbbbbbb");

    ASSERT_TRUE(sink_->append("dddddddd").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 3U);
    EXPECT_EQ(full_.tasks.size(), 1U);
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");
    EXPECT_EQ(DecompressFile(scheduler_.rotate_paths[2] + ".zst"), "cccccccc");
    EXPECT_EQ(ReadFile(active_), "dddddddd");
}

// 测试思路：归档保留上限已满时，sink 仍必须跳过等待 End 的原文，既不能删除也不能补偿。
// 例：上限=1，A/B 两份 End 均 pending；两份 raw 暂存，End 成功后各自实时发布，full=0。
TEST_F(MmapSinkCompensationTest, PendingEndProtectsRawFromCleanupAndCompensation)
{
    config_.rotate_max_backup_files = 1;
    registry_.setConfig(config_);
    scheduler_.auto_end = false;
    full_.accept = true;
    ASSERT_TRUE(sink_->append("aaaaaaaa").ok());
    ASSERT_TRUE(sink_->append("bbbbbbbb").ok());
    ASSERT_TRUE(sink_->append("cccccccc").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 2U);
    const auto first = scheduler_.rotate_paths[0];
    const auto second = scheduler_.rotate_paths[1];
    EXPECT_TRUE(backend_->archiveInUse(first));
    EXPECT_TRUE(backend_->archiveInUse(second));
    EXPECT_EQ(ReadFile(first), "aaaaaaaa");
    EXPECT_EQ(ReadFile(second), "bbbbbbbb");
    EXPECT_EQ(ReadFile(active_), "cccccccc");
    EXPECT_FALSE(sink_->finishArchiveCompletions().ok());
    EXPECT_TRUE(full_.tasks.empty());

    scheduler_.finishEnds();
    ASSERT_TRUE(sink_->finishArchiveCompletions().ok());
    EXPECT_FALSE(backend_->archiveInUse(first));
    EXPECT_FALSE(backend_->archiveInUse(second));
    EXPECT_EQ(DecompressFile(first + ".zst"), "aaaaaaaa");
    EXPECT_EQ(DecompressFile(second + ".zst"), "bbbbbbbb");
    EXPECT_FALSE(std::filesystem::exists(first));
    EXPECT_FALSE(std::filesystem::exists(second));
    EXPECT_TRUE(full_.tasks.empty());
}

// 测试思路：full 拒绝受理不会占用归档，后续成功轮转可重新提交；一旦受理则不重复提交。
// 例：A 的 End 被拒绝，B 轮转时补偿 A 被拒，C 轮转重试成功，D 轮转时累计尝试仍为 2。
TEST_F(MmapSinkCompensationTest, RejectedCompensationRetriesOnLaterRotation)
{
    scheduler_.accept_end = false;
    ASSERT_TRUE(sink_->append("aaaaaaaa").ok());
    ASSERT_TRUE(sink_->append("bbbbbbbb").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 1U);
    const auto old_archive = scheduler_.rotate_paths.front();
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");

    scheduler_.accept_end = true;
    ASSERT_TRUE(sink_->append("cccccccc").ok());
    ASSERT_EQ(full_.tasks.size(), 1U);
    EXPECT_EQ(full_.tasks[0].archive_log_path, old_archive);
    EXPECT_FALSE(backend_->archiveInUse(old_archive));
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");

    full_.accept = true;
    ASSERT_TRUE(sink_->append("dddddddd").ok());
    ASSERT_EQ(full_.tasks.size(), 2U);
    EXPECT_EQ(full_.tasks[1].archive_log_path, old_archive);
    EXPECT_EQ(full_.tasks[1].archive_compression_path, old_archive + ".zst");
    EXPECT_NE(full_.tasks[0].task_id, full_.tasks[1].task_id);
    EXPECT_TRUE(backend_->archiveInUse(old_archive));
    ASSERT_TRUE(sink_->append("eeeeeeee").ok());
    EXPECT_EQ(full_.tasks.size(), 2U);
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");
    EXPECT_EQ(ReadFile(active_), "eeeeeeee");
}

// 测试思路：明确当前策略的触发边界：没有后续轮转时，flush/收尾/关闭只保留失败 raw。
// 例：A 的 End=EIO，B 留在 active；反复 flush 与最终 close 均成功，但不会提交 A 的补偿。
TEST_F(MmapSinkCompensationTest, FlushAndShutdownRetainRawWithoutStartingCompensation)
{
    scheduler_.auto_end = false;
    full_.accept = true;
    ASSERT_TRUE(sink_->append("aaaaaaaa").ok());
    ASSERT_TRUE(sink_->append("bbbbbbbb").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 1U);
    const auto old_archive = scheduler_.rotate_paths.front();
    scheduler_.finishEnds(std::make_error_code(std::errc::io_error));
    ASSERT_TRUE(sink_->flush().ok());
    ASSERT_TRUE(sink_->finishArchiveCompletions().ok());
    ASSERT_TRUE(sink_->flush().ok());
    ASSERT_TRUE(sink_->finishArchiveCompletions().ok());
    ASSERT_TRUE(sink_->closeForShutdown().ok());
    EXPECT_FALSE(backend_->isOpen());
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");
    EXPECT_FALSE(std::filesystem::exists(old_archive + ".zst"));
    EXPECT_EQ(ReadFile(active_), "bbbbbbbb");
}

// 测试思路：sink 先清理超限归档、再补偿；不强制压缩已经超出保留范围且不在途的 raw。
// 例：上限=1，A 的 End 被拒，B 轮转后 A 超限被删，B 实时发布，full=0。
TEST_F(MmapSinkCompensationTest, RetentionCleanupPrecedesCompensation)
{
    config_.rotate_max_backup_files = 1;
    registry_.setConfig(config_);
    scheduler_.accept_end = false;
    full_.accept = true;
    ASSERT_TRUE(sink_->append("aaaaaaaa").ok());
    ASSERT_TRUE(sink_->append("bbbbbbbb").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 1U);
    const auto old_archive = scheduler_.rotate_paths.front();
    EXPECT_EQ(ReadFile(old_archive), "aaaaaaaa");
    EXPECT_FALSE(backend_->archiveInUse(old_archive));

    scheduler_.accept_end = true;
    ASSERT_TRUE(sink_->append("cccccccc").ok());
    ASSERT_EQ(scheduler_.rotate_paths.size(), 2U);
    EXPECT_FALSE(std::filesystem::exists(old_archive));
    EXPECT_FALSE(std::filesystem::exists(old_archive + ".zst"));
    EXPECT_EQ(DecompressFile(scheduler_.rotate_paths[1] + ".zst"), "bbbbbbbb");
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_EQ(ReadFile(active_), "cccccccc");
}

// 测试思路：active 写失败即使 compression 成功也不能 reset committed slot；例：ENOSPC 后拒绝轮转/open。
TEST_F(MmapBackendTest, ActiveFailureRetainsCommittedSlotAndRequiresRecovery)
{
    scheduler_.active_error = std::make_error_code(std::errc::no_space_on_device);
    ASSERT_TRUE(backend_->append("12345678").ok());
    ASSERT_EQ(scheduler_.batches.size(), 1U);
    EXPECT_FALSE(scheduler_.batches[0]->completion().canReleaseSlot());
    EXPECT_FALSE(backend_->flush().ok());
    EXPECT_FALSE(backend_->rotate(request(), 20).ok());
    EXPECT_FALSE(backend_->append("new").ok());
    EXPECT_FALSE(std::filesystem::exists(archive_));
    const auto path = MmapSlotPaths::MakePaths(active_).path[0];
    MmapSlot slot;
    ASSERT_TRUE(slot.open(path, 64).ok());
    EXPECT_EQ(slot.header().payload_size, 8U);
    EXPECT_EQ(static_cast<MmapSlotState>(slot.header().state), MmapSlotState::kProcessing);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(slot.payload().data()), slot.payload().size()), "12345678");
}

// 测试思路：超大记录不能拆分、部分写入或改变 sequence；例：64B slot 拒绝 65B，随后 ok 正常写入。
TEST_F(MmapBackendTest, OversizedRecordIsRejectedWithoutPartialAppend)
{
    EXPECT_FALSE(backend_->append(std::string(65, 'x')).ok());
    EXPECT_EQ(backend_->lastSequence(), 0U);
    EXPECT_TRUE(scheduler_.batches.empty());
    ASSERT_TRUE(backend_->append("ok").ok());
    ASSERT_TRUE(backend_->flush().ok());
    EXPECT_EQ(ReadFile(active_), "ok");
}

// 测试思路：close 不能偷偷 flush 或 unmap 未封口尾部；例：tail 保留直到显式 flush，再 close 成功。
TEST_F(MmapBackendTest, CloseDefersUntilWritableTailIsFlushed)
{
    ASSERT_TRUE(backend_->append("tail").ok());
    backend_->close();
    EXPECT_TRUE(backend_->isOpen());
    EXPECT_EQ(ReadFile(active_), "");
    ASSERT_TRUE(backend_->flush().ok());
    backend_->close();
    EXPECT_FALSE(backend_->isOpen());
    EXPECT_EQ(ReadFile(active_), "tail");
}

// 测试思路：按当前 .bck 策略，第一次发布冲突可以另存完整压缩流，但不能覆盖既有 .zst。
// 例：预置 .zst="keep"，本次 raw="raw"；.bck 解压为 raw，成功后原文可删除，补偿 0 次。
TEST_F(MmapBackendTest, PublicationConflictPublishesBackupWithoutOverwritingOriginal)
{
    WriteFile(archive_ + ".zst", "keep");
    ASSERT_TRUE(backend_->append("raw").ok());
    ASSERT_TRUE(backend_->rotate(request(), 1000).ok());
    EXPECT_EQ(ReadFile(archive_ + ".zst"), "keep");
    ASSERT_TRUE(std::filesystem::exists(archive_ + ".zst.bck"));
    EXPECT_EQ(DecompressFile(archive_ + ".zst.bck"), "raw");
    EXPECT_FALSE(std::filesystem::exists(archive_));
    EXPECT_TRUE(full_.tasks.empty());
    EXPECT_FALSE(backend_->lastRotationReport().compression_error);
    EXPECT_EQ(backend_->lastRotationReport().disposition, MmapArchiveDisposition::kRealtimePublished);
}

// 测试思路：两 slot 按 sequence 而非物理下标恢复，并包含原 active 前缀进入实时压缩流。
// 例：slot0=(7,2,"two"), slot1=(7,1,"one",Processing)，归档解压=prefixonetwo。
TEST(MmapRecoveryTest, ReplaysProcessingAndSealedInSequenceWithExistingPrefix)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    WriteFile(active, "prefix");
    SeedSlot(active, 0, 7, 2, "two", MmapSlotState::kSealed);
    SeedSlot(active, 1, 7, 1, "one", MmapSlotState::kProcessing);
    CapturingScheduler scheduler;
    RecordingRecompressScheduler full;
    MmapBufferedLogFileBackend backend(scheduler, full, {64, 8, true, 1000ms});
    ASSERT_TRUE(backend.open(active).ok());
    EXPECT_EQ(ReadFile(active), "prefixonetwo");
    EXPECT_EQ(backend.openSize(), 12U);
    EXPECT_EQ(backend.generation(), 7U);
    EXPECT_EQ(backend.lastSequence(), 2U);
    LogFileRotateRequest request{7, 2, active, directory.file("archive.log"), true};
    ASSERT_TRUE(backend.rotate(request, 1000).ok());
    EXPECT_EQ(DecompressFile(request.archive_log_path + ".zst"), "prefixonetwo");
    backend.close();
}

// 测试思路：两个 slot 必须先全部检查再重放；例：第二块 CRC/代次/序号错误时第一块也不能写 active。
// 原始 slot 文件逐字节保留，以便后续人工恢复；Writable 不属于已提交数据。
TEST(MmapRecoveryTest, RejectsInvalidPairBeforeAnyReplayAndPreservesEvidence)
{
    for (const std::string failure : {"generation", "gap", "duplicate", "crc", "header"})
    {
        SCOPED_TRACE(failure);
        TempDirectory directory;
        const auto active = directory.file("active.log");
        WriteFile(active, "prefix");
        SeedSlot(active, 0, 7, 1, "one", MmapSlotState::kProcessing);
        SeedSlot(active, 1, failure == "generation" ? 8 : 7,
            failure == "gap" ? 3 : failure == "duplicate" ? 1 : 2, "two", MmapSlotState::kSealed);
        const auto paths = MmapSlotPaths::MakePaths(active);
        if (failure == "crc" || failure == "header")
        {
            MmapSlot slot;
            ASSERT_TRUE(slot.open(paths.path[1], 64).ok());
            if (failure == "crc") slot.payload()[0] ^= 1;
            else slot.header().magic ^= 1;
        }
        const auto first = ReadFile(paths.path[0]);
        const auto second = ReadFile(paths.path[1]);
        CapturingScheduler scheduler;
        RecordingRecompressScheduler full;
        MmapBufferedLogFileBackend backend(scheduler, full, {64, 8, true, 1000ms});
        const auto result = backend.open(active);
        EXPECT_EQ(result.status, LogBackendStatus::kOpenFailed);
        EXPECT_FALSE(backend.isOpen());
        EXPECT_TRUE(scheduler.batches.empty());
        EXPECT_EQ(ReadFile(active), "prefix");
        EXPECT_EQ(ReadFile(paths.path[0]), first);
        EXPECT_EQ(ReadFile(paths.path[1]), second);
    }
}

// 测试思路：Writable 未到 commit 点不能重放；例：崩溃前未 seal 的 lost 丢弃，新写 kept 正常提交。
TEST(MmapRecoveryTest, DiscardsOnlyUncommittedWritableTail)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    SeedSlot(active, 0, 7, 1, "lost", MmapSlotState::kWritable);
    CapturingScheduler scheduler;
    RecordingRecompressScheduler full;
    MmapBufferedLogFileBackend backend(scheduler, full, {64, 8, true, 1000ms});
    ASSERT_TRUE(backend.open(active).ok());
    EXPECT_TRUE(scheduler.batches.empty());
    ASSERT_TRUE(backend.append("kept").ok());
    EXPECT_TRUE(backend.flush().ok());
    EXPECT_EQ(ReadFile(active), "kept");
    backend.close();
}

// 测试思路：压缩关闭时只保留文本归档，不提交 End/补偿；例：旧 raw="old"、新 active="new"。
TEST(MmapIntegrationTest, CompressionDisabledProducesRawArchiveOnly)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    const auto archive = directory.file("archive.log");
    CapturingScheduler scheduler;
    RecordingRecompressScheduler full;
    MmapBufferedLogFileBackend backend(scheduler, full, {64, 8, false, 1000ms});
    ASSERT_TRUE(backend.open(active).ok());
    ASSERT_TRUE(backend.append("old").ok());
    EXPECT_TRUE(backend.rotate({backend.generation(), backend.lastSequence(), active, archive, false}, 1000).ok());
    EXPECT_EQ(scheduler.end_attempts, 0U);
    EXPECT_TRUE(full.tasks.empty());
    EXPECT_EQ(backend.lastRotationReport().disposition, MmapArchiveDisposition::kRawOnly);
    EXPECT_EQ(ReadFile(archive), "old");
    ASSERT_TRUE(backend.append("new").ok());
    EXPECT_TRUE(backend.flush().ok());
    backend.close();
    EXPECT_EQ(ReadFile(active), "new");
    EXPECT_FALSE(std::filesystem::exists(archive + ".zst"));
}

// 测试思路：冷启动遇到旧实时临时文件必须隔离/删除，不能让 O_EXCL 冲突使新代降级。
// 例：预置 active.g1.zst.tmp="stale"，重启后旧 active 前缀+新 batch 都应实时压缩成功。
TEST(MmapRecoveryTest, StaleRealtimeTemporaryDoesNotPoisonNewSession)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    const auto archive = directory.file("archive.log");
    WriteFile(active, "prefix");
    WriteFile(active + ".g1.zst.tmp", "stale");
    CapturingScheduler scheduler;
    RecordingRecompressScheduler full;
    MmapBufferedLogFileBackend backend(scheduler, full, {64, 8, true, 1000ms});
    ASSERT_TRUE(backend.open(active).ok());
    ASSERT_TRUE(backend.append("tail").ok());
    EXPECT_TRUE(backend.rotate({backend.generation(), backend.lastSequence(), active, archive, true}, 1000).ok());
    EXPECT_TRUE(full.tasks.empty());
    EXPECT_EQ(backend.lastRotationReport().disposition, MmapArchiveDisposition::kRealtimePublished);
    if (std::filesystem::exists(archive + ".zst"))
        EXPECT_EQ(DecompressFile(archive + ".zst"), "prefixtail");
    else
        ADD_FAILURE() << "realtime archive was not published";
    backend.close();
}

// 测试思路：冷恢复只清理当前 sink 的“g+数字+.zst.tmp”，不能误删其他 sink 或非实时归档。
// 例：active.log.g1.zst.tmp/g23.zst.tmp 被清理，other.log.g1.zst.tmp、gx、无代次及已发布 .zst 保持原样。
TEST(MmapRecoveryTest, StaleTemporaryCleanupIsScopedToSinkAndGenerationPattern)
{
    TempDirectory directory;
    const auto active = directory.file("active.log");
    const std::vector<std::string> stale = {active + ".g1.zst.tmp", active + ".g23.zst.tmp"};
    const std::vector<std::string> preserved = {
        directory.file("other.log.g1.zst.tmp"), active + ".gx.zst.tmp",
        active + ".g.zst.tmp", active + ".g1.zst", active + ".zst.tmp",
        active + ".g1.zst.tmp.bck"};
    for (const auto& path : stale) WriteFile(path, "stale");
    for (const auto& path : preserved) WriteFile(path, "keep");
    CapturingScheduler scheduler;
    RecordingRecompressScheduler full;
    MmapBufferedLogFileBackend backend(scheduler, full, {64, 8, true, 1000ms});
    ASSERT_TRUE(backend.open(active).ok());
    // 空 active 尚未建立新压缩 session，此处可直接检查旧文件是否已清理。
    for (const auto& path : stale) EXPECT_FALSE(std::filesystem::exists(path)) << path;
    for (const auto& path : preserved) EXPECT_EQ(ReadFile(path), "keep") << path;
    backend.close();
}

// 测试思路：真实双执行器反复轮转多个 sink，压缩流及代次必须隔离；例：A0/A1 与 B0/B1 互不串写。
TEST(MmapIntegrationTest, RealWorkersPublishIndependentGenerationsForTwoSinks)
{
    TempDirectory directory;
    MmapBatchDispatcher dispatcher;
    FullRecompressWorker full({});
    dispatcher.start();
    full.start();
    MmapBufferedLogFileBackend a(dispatcher, full, {64, 8, true, 3000ms});
    MmapBufferedLogFileBackend b(dispatcher, full, {64, 8, true, 3000ms});
    const auto ap = directory.file("a.log"), bp = directory.file("b.log");
    ASSERT_TRUE(a.open(ap).ok());
    ASSERT_TRUE(b.open(bp).ok());
    // worker/slot 生命周期在任何断言失败路径都按 drain -> poll -> close 收拢。
    struct Cleanup {
        MmapBatchDispatcher& dispatcher;
        FullRecompressWorker& full;
        MmapBufferedLogFileBackend& a;
        MmapBufferedLogFileBackend& b;
        ~Cleanup() {
            (void)a.flush(); (void)b.flush();
            dispatcher.stopAccepting(); dispatcher.wait();
            a.pollArchiveCompletions(); b.pollArchiveCompletions();
            full.stopAccepting(); full.wait();
            a.close(); b.close();
        }
    } cleanup{dispatcher, full, a, b};
    for (int round = 0; round < 3; ++round)
    {
        const std::string av = "A" + std::to_string(round) + std::string(45, 'a');
        const std::string bv = "B" + std::to_string(round) + std::string(43, 'b');
        ASSERT_TRUE(a.append(av).ok());
        ASSERT_TRUE(b.append(bv).ok());
        const auto aa = directory.file("a" + std::to_string(round) + ".log");
        const auto ba = directory.file("b" + std::to_string(round) + ".log");
        ASSERT_TRUE(a.rotate({a.generation(), a.lastSequence(), ap, aa, true}, 3000).ok());
        ASSERT_TRUE(b.rotate({b.generation(), b.lastSequence(), bp, ba, true}, 3000).ok());
        ASSERT_TRUE(dispatcher.drain(3000));
        a.pollArchiveCompletions(); b.pollArchiveCompletions();
        ASSERT_TRUE(full.drain(3000));
        EXPECT_EQ(DecompressFile(aa + ".zst"), av);
        EXPECT_EQ(DecompressFile(ba + ".zst"), bv);
        EXPECT_FALSE(a.hasPendingArchives());
        EXPECT_FALSE(b.hasPendingArchives());
    }
    EXPECT_EQ(ReadFile(ap), "");
    EXPECT_EQ(ReadFile(bp), "");
}
