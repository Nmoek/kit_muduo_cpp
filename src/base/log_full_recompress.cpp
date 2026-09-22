/**
 * @file log_full_recompress.cpp
 * @brief 日志全量压缩
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 16:24:27
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_full_recompress.h"
#include "base/defer.h"
#include "base/log_inner.h"
#include "base/util.h"
#include <chrono>
#include <exception>
#include <filesystem>
#include <system_error>

namespace kit_muduo {

namespace {

class ArchiveFd : Noncopyable {
public:
    explicit ArchiveFd(int fd = -1) noexcept
        : fd_(fd)
    { }
    ~ArchiveFd() { (void)close(); }
    int fd() const noexcept { return fd_; }
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

    inline std::error_code WriteArchiveBytes(Span<const uint8_t> bytes) noexcept
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

    inline std::error_code SyncArchiveFd() noexcept
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
private:
    int fd_;
};



} // namespace

FullRecompressWorker::FullRecompressWorker(std::shared_ptr<CompressionCodec> codec)
    :FullRecompressWorker(std::move(codec), Options{})
{

}

FullRecompressWorker::FullRecompressWorker(std::shared_ptr<CompressionCodec> codec, Options options)
    :compress_codec_(std::move(codec))
    ,options_(std::move(options))
    ,thread_([this] { workLoop(); }, "log-recompress")
{
    if (!compress_codec_ || options.queue_capacity == 0 || options.read_buffer_bytes == 0)
    {
        throw std::invalid_argument("full recompress worker options invalid");
    }
}

FullRecompressWorker::~FullRecompressWorker()
{
    wait();
}

void FullRecompressWorker::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_)
    {
        return;
    }
    if (closed_)
    {
        throw std::logic_error("full recompress worker cannot restart");
    }
    thread_.start();
    started_ = true;
    accepting_ = true;
}

bool FullRecompressWorker::submit(FullRecompressTask task) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!accepting_ || task.task_id == 0 || task.archive_log_path.empty()
        || task.archive_compression_path.empty()
        || task.archive_log_path == task.archive_compression_path
        || queue_.size() >= options_.queue_capacity)
    {
        return false;
    }

    // 归档路径由 sink 提供 normalized 唯一名称；已接收路径本次运行不重复压缩。
    bool inserted = false;
    bool is_exception = false;
    const std::string& key = task.archive_log_path;



    try {
        inserted = queued_records_.emplace(key).second;
        if(!inserted)
        {
            LOG_INNER_INFO("full recompress task duplicate! key[%s]\n", key.c_str());
            return false;
        }
        queue_.push(task);

    } catch (const std::exception &e) {
        LOG_INNER_EXCPTION("log full recompress work exception: %s \n", e.what());
        is_exception = true;
    } catch (...) {
        LOG_INNER_EXCPTION("log full recompress work unknown exception \n");
        is_exception = true;
    }
    if(is_exception)
    {
        if(inserted)
        {
            queued_records_.erase(key);
        }
        return false;
    }

    ++outstanding_;  // 入队和计数共用锁，避免 pop 到执行之间 drain 提前返回。
    cv_.notify_all();
    return true;
}


void FullRecompressWorker::stopAccepting() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
}

bool FullRecompressWorker::drain(uint64_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return outstanding_ == 0; });
}

void FullRecompressWorker::wait() noexcept
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        closed_ = true;
        cv_.notify_all();
    }
    thread_.join();
}


FullRecompressWorker::Result FullRecompressWorker::compressOne(const FullRecompressTask& task) noexcept
{
    Result result;
    std::string tmp;
    std::error_code error;
    // 函数退出时 需要清理tmp文件
    Defer df1([&tmp]{
        if(tmp.empty())
        {
            return;
        }
        std::error_code error;
        (void)std::filesystem::remove(tmp.c_str(), error);
        if(error)
        {
            LOG_INNER_INFO("log compress tmp remove error: %s\n", error.message().c_str());
        }
    });

    try {
        tmp = task.archive_compression_path + ".tmp." +
            std::to_string(GetPid()) + "." + std::to_string(task.task_id);

        ArchiveFd input(::open(task.archive_log_path.c_str(), O_RDONLY | O_CLOEXEC));
        if(input.fd() < 0)
        {
            throw std::system_error(errno, std::generic_category(), "open log archive: " + tmp);
        }

        ArchiveFd output(::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644));
        if(output.fd() < 0)
        {
            throw std::system_error(errno, std::generic_category(), "create tmp");
        }
        auto compressor = compress_codec_->createCompressor();
        if (!compressor)
        {
            throw std::runtime_error("null stream compressor");
        }

        std::vector<uint8_t> buffer(options_.read_buffer_bytes);

        uint64_t input_bytes = 0;
        uint64_t output_bytes = 0;
        const CompressionCallback emit_cb = [&output, &output_bytes](Span<const uint8_t> bytes) {
            const auto error = output.WriteArchiveBytes(bytes);
            if(error)
            {
                return CompressionCallbackResult::Failure(error.message());
            }
            output_bytes += bytes.size();
            return CompressionCallbackResult::Ok();
        };

        for (;;)
        {
            // 防止及时获取文件存在与否状态
            if(!std::filesystem::exists(task.archive_log_path.c_str(), error) || error)
            {
                throw std::system_error(error, "log archive not exists");
            }

            const auto n = ::read(input.fd(), buffer.data(), buffer.size());
            if(n == 0)
            {
                break;
            }
            if(n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::system_error(errno, std::generic_category(), "log archive read");
            }

            auto step = compressor->write(Span<const uint8_t>(buffer.data(), static_cast<size_t>(n)), emit_cb);
            input_bytes += step.input_bytes;

            if (!step.ok())
            {
                result.compression_result = std::move(step);
                result.compression_result.input_bytes = input_bytes;
                result.compression_result.output_bytes = output_bytes;

                return result;
            }
        }
        result.compression_result = compressor->finish(emit_cb);

        result.compression_result.input_bytes += input_bytes;
        result.compression_result.output_bytes = output_bytes;

        if (!result.compression_result.ok())
        {
            return result;
        }

        if (auto error = output.SyncArchiveFd())
        {
            throw std::system_error(error);
        }
        if (auto error = output.close())
        {
            throw std::system_error(error);
        }
        if (auto error = input.close())
        {
            throw std::system_error(error);
        }

        // archive 名称唯一；同目录 rename 保证原子发布，不覆盖其他任务的目标。
        std::filesystem::rename(tmp.c_str(), task.archive_compression_path.c_str(), error);
        if(error)
        {
            throw std::system_error(error, "publish log archive");
        }
        result.published = true;
        tmp.clear();

        if (!std::filesystem::remove(task.archive_log_path.c_str(), error) || error)
        {
            result.cleanup_error = std::move(error);
        }
        return result;
    } catch (const std::exception& e) {
        result.compression_result = CompressionResult::Failure(
            CompressionResultStatus::kInternalError, e.what());
    } catch (...) {
        result.compression_result = CompressionResult::Failure(
            CompressionResultStatus::kInternalError, "log full recompression exception");
    }

    return result;
}

void FullRecompressWorker::workLoop() noexcept
{
    // 问题：
    /*
        1. records_ 到底记录的是队列任务情况还是实际的磁盘情况
        2. records_的淘汰策略  现在只增长不淘汰
        3. 内存记录和磁盘不对应怎么办
    */
    for (;;)
    {
        FullRecompressTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
            if(queue_.empty())
            {
                break;
            }
            task = std::move(queue_.front());
            queue_.pop();

        }
        LOG_INNER_DEBUG("log full recompre taskId[%lu] generation[%lu] path[%s] \n", task.task_id, task.generation, task.archive_log_path.c_str());
        // 注意：锁外压缩
        Result result = compressOne(task);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queued_records_.erase(task.archive_log_path);
            --outstanding_;
            cv_.notify_all();
        }
    }
    LOG_INNER_DEBUG("work loop exit....\n");
}


} // namespace kit_muduo