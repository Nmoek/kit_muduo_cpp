/**
 * @file test_log_full_recompress.cpp
 * @brief 普通日志轮转与全量压缩链路测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-21
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/compression.h"
#include "base/log_config.h"
#include "base/log_file_sink.h"
#include "base/log_full_recompress.h"
#include "base/regular_log_file_backend.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

using namespace kit_muduo;

namespace {

class TempLogDirectory
{
public:
    explicit TempLogDirectory(const std::string& case_name)
        : path_(std::filesystem::temp_directory_path()
            / ("kit_log_full_recompress_" + std::to_string(::getpid())
                + "_" + case_name))
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (!std::filesystem::create_directories(path_, error) || error)
        {
            throw std::runtime_error("create temporary log directory failed: "
                + error.message());
        }
    }

    ~TempLogDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("open test file for write failed: " + path.string());
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output)
    {
        throw std::runtime_error("write test file failed: " + path.string());
    }
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("open test file for read failed: " + path.string());
    }
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string ReadFile(const std::filesystem::path& path)
{
    const auto bytes = ReadFileBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

std::string DecompressFile(const std::filesystem::path& path)
{
    const auto compressed = ReadFileBytes(path);
    auto codec = CompressionCodec::Create();
    if (!codec)
    {
        throw std::runtime_error("create compression codec failed");
    }

    std::vector<uint8_t> output;
    const auto result = codec->decompress(
        Span<const uint8_t>(compressed.data(), compressed.size()),
        [&output](Span<const uint8_t> bytes) {
            output.insert(output.end(), bytes.begin(), bytes.end());
            return CompressionCallbackResult::Ok();
        });
    if (!result.ok())
    {
        throw std::runtime_error("decompress test archive failed: " + result.message);
    }
    return std::string(output.begin(), output.end());
}

FullRecompressTask MakeTask(const std::filesystem::path& source,
    const std::filesystem::path& target,
    uint64_t generation = 0)
{
    return FullRecompressTask{
        FullRecompressTask::NextTaskId(),
        generation,
        source.string(),
        target.string(),
    };
}

class RejectingFullRecompressScheduler final : public FullRecompressScheduler
{
public:
    bool submit(FullRecompressTask task) noexcept override
    {
        ++submit_count_;
        last_task_ = std::move(task);
        return false;
    }

    std::string compressSuffix() const noexcept override { return "zst"; }
    bool archiveInUse(const std::string& path) const noexcept override { return path == protected_path_; }
    void protectArchive(std::string path) { protected_path_ = std::move(path); }

    size_t submitCount() const noexcept { return submit_count_; }
    const FullRecompressTask& lastTask() const noexcept { return last_task_; }

private:
    std::string protected_path_;
    size_t submit_count_{0};
    FullRecompressTask last_task_;
};

} // namespace

/*
测试思路：使用小于业务默认值的读取缓冲区，强制一个归档经过多次 read/write，验证
FullRecompressWorker 在 drain 返回前已原子发布 zstd 文件，并且只有发布成功后才删除
源日志。最后解压产物并逐字节比对，覆盖普通文件全量压缩的核心数据完整性。

调用路径：submit -> FullRecompressWorker::workLoop -> compressOne -> rename -> unlink。
示例：4099 字节 payload、64 字节读取缓冲区 -> archive.log.zst 解压后仍为 4099 字节。
*/
TEST(LogFullRecompressTest, WorkerPublishesCompleteArchiveAndRemovesSource)
{
    TempLogDirectory directory("worker_success");
    const auto source = directory.path() / "archive.log";
    const auto target = directory.path() / "archive.log.zst";
    std::string payload(4099, 'x');
    payload[0] = 'A';
    payload[2048] = '\0';
    payload.back() = 'Z';
    WriteFile(source, payload);

    FullRecompressWorker worker(FullRecompressWorker::Options{
        .queue_capacity = 4,
        .read_buffer_bytes = 64,
    });
    worker.start();

    ASSERT_TRUE(worker.submit(MakeTask(source, target, 7)));
    ASSERT_TRUE(worker.drain(5000));

    EXPECT_FALSE(std::filesystem::exists(source));
    ASSERT_TRUE(std::filesystem::exists(target));
    EXPECT_EQ(DecompressFile(target), payload);

    worker.stopAccepting();
    worker.wait();
}

/*
测试思路：先让压缩目标的父目录不存在，使第一次任务失败并保留源日志；drain 返回后
创建目标目录，再对同一个源路径提交新的补偿任务。第二次任务必须被接受并成功发布，
证明失败终态已经释放在途路径，同时验证补偿完成前不会丢失原始日志。

调用路径：首次 submit -> open(tmp) 失败 -> 释放在途路径 -> 再次 submit -> 发布 zstd。
示例：target 父目录首次不存在时 source 保留；创建父目录后重提同一路径，最终 source
删除且 target 解压内容等于原文。
*/
TEST(LogFullRecompressTest, WorkerRetainsSourceAndAllowsLaterCompensationAfterFailure)
{
    TempLogDirectory directory("worker_failure");
    const auto source = directory.path() / "archive.log";
    const auto target = directory.path() / "missing" / "archive.log.zst";
    const std::string payload = "source must survive compression failure\n";
    WriteFile(source, payload);

    FullRecompressWorker worker(FullRecompressWorker::Options{});
    worker.start();

    ASSERT_TRUE(worker.submit(MakeTask(source, target, 3)));
    ASSERT_TRUE(worker.drain(5000));

    EXPECT_TRUE(std::filesystem::exists(source));
    EXPECT_EQ(ReadFile(source), payload);
    EXPECT_FALSE(std::filesystem::exists(target));

    ASSERT_TRUE(std::filesystem::create_directories(target.parent_path()));
    ASSERT_TRUE(worker.submit(MakeTask(source, target, 4)));
    ASSERT_TRUE(worker.drain(5000));

    EXPECT_FALSE(std::filesystem::exists(source));
    ASSERT_TRUE(std::filesystem::exists(target));
    EXPECT_EQ(DecompressFile(target), payload);

    worker.stopAccepting();
    worker.wait();
}

/*
测试思路：直接装配真实 RegularLogFileBackend 与 FullRecompressWorker，写入第一代 active
后发起轮转，再立即写入第二代 active。等待 worker 排空后验证第一代归档已压缩并删除
文本源文件，压缩内容等于第一代数据，而新 active 只包含第二代数据。

调用路径：RegularLogFileBackend::rotate -> scheduler.submit -> FullRecompressWorker。
示例：active 写 "generation-0"，轮转后写 "generation-1" -> 两个代际互不混合。
*/
TEST(LogFullRecompressTest, RegularBackendRotatesCompressesAndContinuesWriting)
{
    TempLogDirectory directory("regular_backend");
    const auto active = directory.path() / "service.log";
    const auto archive = directory.path() / "service_001_20260921-000000-000.log";
    const auto compressed = archive.string() + ".zst";
    const std::string first_generation = "generation-0\n";
    const std::string second_generation = "generation-1\n";

    FullRecompressWorker worker(FullRecompressWorker::Options{});
    worker.start();
    RegularLogFileBackend backend(worker);

    ASSERT_TRUE(backend.open(active.string()).ok());
    ASSERT_TRUE(backend.append(first_generation).ok());
    ASSERT_TRUE(backend.rotate(LogFileRotateRequest{
        .generation = backend.generation(),
        .last_sequence = backend.lastSequence(),
        .active_path = active.string(),
        .archive_log_path = archive.string(),
        .compression_enabled = true,
    }).ok());
    ASSERT_TRUE(backend.append(second_generation).ok());
    ASSERT_TRUE(worker.drain(5000));

    EXPECT_EQ(backend.generation(), 1U);
    EXPECT_TRUE(backend.isOpen());
    EXPECT_EQ(ReadFile(active), second_generation);
    EXPECT_FALSE(std::filesystem::exists(archive));
    ASSERT_TRUE(std::filesystem::exists(compressed));
    EXPECT_EQ(DecompressFile(compressed), first_generation);

    backend.close();
    worker.stopAccepting();
    worker.wait();
}

/*
测试思路：预置两个已压缩归档、一个待补偿的 .log 和一个压缩临时文件，再通过真实
LogFileSink 触发下一次轮转。保留数为 3 时，本轮新归档占用一个名额，扫描到的三个
旧归档中只删除日期最早的一个；即使它的序号较大，也不能因为序号回环假设而误删
更新日期的归档。清理后留下的 .log 会再提交一次补偿，.tmp 不参与扫描。

调用路径：LogFileSink::append -> scanArchives -> backend.rotate -> cleanupOldArchives
-> compensateOldArchives。
示例：999/20260919-000.log.zst、001/20260920-001.log.zst、002/20260920-002.log
-> 删除 999/20260919-000.log.zst，并分别提交本轮新归档与
002/20260920-002.log 的压缩任务。
*/
TEST(LogFullRecompressTest, SinkRetentionSortsArchivesAndCompensatesSources)
{
    TempLogDirectory directory("compressed_retention");
    const auto active = directory.path() / "service.log";
    const auto oldest = directory.path() / "service_999_20260919-000000-000.log.zst";
    const auto newest = directory.path() / "service_001_20260920-000001-001.log.zst";
    const auto source = directory.path() / "service_002_20260920-000002-002.log";
    const auto temporary = directory.path()
        / "service_002_20260920-000002-002.log.zst.tmp.1.2";
    WriteFile(oldest, "oldest-zstd-placeholder");
    WriteFile(newest, "newest-zstd-placeholder");
    WriteFile(source, "uncompressed-source");
    WriteFile(temporary, "compression-in-progress");

    RejectingFullRecompressScheduler scheduler;
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.rotate_max_bytes = 4;
    config.rotate_max_backup_files = 3;
    config.compress_rotated = true;
    registry.setConfig(config);

    auto backend = std::make_unique<RegularLogFileBackend>(scheduler);
    auto sink = std::make_shared<LogFileSink>(
        &registry, active.string(), std::move(backend));
    ASSERT_TRUE(sink->reopen().ok());
    ASSERT_TRUE(sink->append("aaaa", false, 4).ok());
    ASSERT_TRUE(sink->append("b", false, 1).ok());

    EXPECT_EQ(scheduler.submitCount(), 2U);
    EXPECT_EQ(scheduler.lastTask().generation, 0U);
    EXPECT_EQ(scheduler.lastTask().archive_log_path, source.string());
    EXPECT_FALSE(std::filesystem::exists(oldest));
    EXPECT_TRUE(std::filesystem::exists(newest));
    EXPECT_TRUE(std::filesystem::exists(source));
    EXPECT_TRUE(std::filesystem::exists(temporary));
    EXPECT_TRUE(std::filesystem::exists(scheduler.lastTask().archive_log_path));
    EXPECT_EQ(ReadFile(active), "b");

    sink->close();
}

/*
测试思路：校验 Options 和任务参数，并明确 start/stop/wait 的接收边界。
例：容量=0、读取块=0 构造失败；未 start、task_id=0、同源同目标、已停止提交都返回 false。
*/
TEST(LogFullRecompressTest, ValidatesOptionsTasksAndLifecycle)
{
    EXPECT_THROW(FullRecompressWorker(FullRecompressWorker::Options{0, 64}), std::invalid_argument);
    EXPECT_THROW(FullRecompressWorker(FullRecompressWorker::Options{4, 0}), std::invalid_argument);
    TempLogDirectory directory("lifecycle");
    const auto source = directory.path() / "a.log";
    const auto target = directory.path() / "a.log.zst";
    WriteFile(source, "data");
    FullRecompressWorker worker(FullRecompressWorker::Options{});
    EXPECT_FALSE(worker.submit(MakeTask(source, target)));
    worker.start();
    worker.start();
    auto invalid = MakeTask(source, target);
    invalid.task_id = 0;
    EXPECT_FALSE(worker.submit(invalid));
    EXPECT_FALSE(worker.submit(MakeTask(source, source)));
    EXPECT_FALSE(worker.submit(MakeTask({}, target)));
    EXPECT_FALSE(worker.submit(MakeTask(source, {})));
    ASSERT_TRUE(worker.submit(MakeTask(source, target)));
    worker.stopAccepting();
    EXPECT_FALSE(worker.submit(MakeTask(source, target)));
    EXPECT_TRUE(worker.drain(3000));
    worker.wait();
    worker.wait();
    EXPECT_THROW(worker.start(), std::logic_error);
    EXPECT_FALSE(worker.archiveInUse(source.string()));
    EXPECT_FALSE(worker.archiveInUse(target.string()));
    EXPECT_EQ(DecompressFile(target), "data");
}

/*
测试思路：空文本也需要产生合法 zstd frame，不能用“输出大小非零”替代内容验证。
例：0 字节 archive -> 可解压的空串，源文件删除且 in-flight 路径释放。
*/
TEST(LogFullRecompressTest, EmptyArchiveProducesValidEmptyFrame)
{
    TempLogDirectory directory("empty");
    const auto source = directory.path() / "empty.log";
    const auto target = directory.path() / "empty.log.zst";
    WriteFile(source, "");
    FullRecompressWorker worker(FullRecompressWorker::Options{});
    worker.start();
    ASSERT_TRUE(worker.submit(MakeTask(source, target)));
    ASSERT_TRUE(worker.drain(3000));
    EXPECT_EQ(DecompressFile(target), "");
    EXPECT_FALSE(std::filesystem::exists(source));
    EXPECT_FALSE(worker.archiveInUse(source.string()));
}

/*
测试思路：所有者析构必须先消化已接收任务并 join，再销毁队列/条件变量。
例：两个不同归档连续提交后立即离开作用域，两个压缩文件都能还原且没有遗留临时文件。
*/
TEST(LogFullRecompressTest, DestructorFinishesAcceptedArchives)
{
    TempLogDirectory directory("destructor");
    const auto first = directory.path() / "first.log";
    const auto second = directory.path() / "second.log";
    WriteFile(first, "first");
    WriteFile(second, "second");
    {
        FullRecompressWorker worker(FullRecompressWorker::Options{});
        worker.start();
        EXPECT_TRUE(worker.submit(MakeTask(first, first.string() + ".zst")));
        EXPECT_TRUE(worker.submit(MakeTask(second, second.string() + ".zst")));
    }
    EXPECT_EQ(DecompressFile(first.string() + ".zst"), "first");
    EXPECT_EQ(DecompressFile(second.string() + ".zst"), "second");
    EXPECT_FALSE(std::filesystem::exists(first));
    EXPECT_FALSE(std::filesystem::exists(second));
    for (const auto& entry : std::filesystem::directory_iterator(directory.path()))
        EXPECT_EQ(entry.path().extension(), ".zst");
}

/*
测试思路：regular 轮转即使全量队列拒绝，已提交的 rename 不能回滚或丢数据。
例：旧代 old 留在 archive.log，新代继续写 new；一次轮转只尝试一次压缩提交。
*/
TEST(LogFullRecompressTest, RejectedCompensationPreservesRawAndNewActive)
{
    TempLogDirectory directory("regular_rejected");
    const auto active = directory.path() / "active.log";
    const auto archive = directory.path() / "archive.log";
    RejectingFullRecompressScheduler scheduler;
    RegularLogFileBackend backend(scheduler);
    ASSERT_TRUE(backend.open(active.string()).ok());
    ASSERT_TRUE(backend.append("old").ok());
    ASSERT_TRUE(backend.rotate({backend.generation(), 0, active.string(), archive.string(), true}).ok());
    ASSERT_TRUE(backend.append("new").ok());
    EXPECT_EQ(scheduler.submitCount(), 1U);
    EXPECT_EQ(ReadFile(archive), "old");
    EXPECT_EQ(ReadFile(active), "new");
    EXPECT_EQ(backend.generation(), 1U);
}

/*
测试思路：最老归档正在被 worker 使用时，数量清理必须跳过它并检查其他可删归档。
例：old(保护)/middle/newest + 本轮新归档，保留数=2；保留 old 和本轮归档，删除 middle/newest。
*/
TEST(LogFullRecompressTest, RetentionSkipsProtectedOldestAndRemovesOtherCandidates)
{
    TempLogDirectory directory("retention_inflight");
    const auto active = directory.path() / "service.log";
    const auto oldest = directory.path() / "service_001_20260919-000000-000.log.zst";
    const auto middle = directory.path() / "service_002_20260920-000000-000.log.zst";
    const auto newest = directory.path() / "service_003_20260921-000000-000.log.zst";
    WriteFile(oldest, "protected");
    WriteFile(middle, "middle");
    WriteFile(newest, "newest");
    RejectingFullRecompressScheduler scheduler;
    scheduler.protectArchive(oldest.string());
    LogFileSinkRegister registry;
    LogFileConfig config;
    config.rotate_max_bytes = 4;
    config.rotate_max_backup_files = 2;
    registry.setConfig(config);
    auto sink = std::make_shared<LogFileSink>(&registry, active.string(),
        std::make_unique<RegularLogFileBackend>(scheduler));
    ASSERT_TRUE(sink->reopen().ok());
    ASSERT_TRUE(sink->append("aaaa").ok());
    ASSERT_TRUE(sink->append("b").ok());
    EXPECT_EQ(ReadFile(oldest), "protected");
    EXPECT_FALSE(std::filesystem::exists(middle));
    EXPECT_FALSE(std::filesystem::exists(newest));
    EXPECT_EQ(ReadFile(active), "b");
    sink->close();
}
