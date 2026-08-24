/**
 * @file util.cpp
 * @brief 常用工具
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-18 23:27:02
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/util.h"
#include "base/base_log.h"
#include "cppcodec/base64_rfc4648.hpp"
#include "stduuid/uuid.h"
#include "simdutf.h"

#include <ctime>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <sys/time.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <algorithm>

namespace kit_muduo
{

thread_local pid_t t_thread_id = 0;


pid_t GetPid()
{
    static pid_t cur_pid = ::getpid();
    return cur_pid;
}


pid_t GetPPid()
{
    static pid_t parent_pid = ::getppid();
    return parent_pid;
}


pid_t GetThreadPid()
{
    // 编译优化  告知编译器该分支进入概率偏低
    if(__builtin_expect(t_thread_id == 0, 0))
    {
        t_thread_id = syscall(SYS_gettid);
    }
    return t_thread_id;
}

pthread_t GetThreadTid()
{
    return ::pthread_self();
}

std::string GetThreadName()
{
    char thread_name[32] = {0};
    ::pthread_getname_np(::pthread_self(), thread_name, sizeof(thread_name));
    return thread_name;
}

/**
 * @brief 创建eventfd句柄
 * @return int32_t
 */
int32_t CreateEventFd()
{
    int32_t evfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evfd < 0)
    {
        BASE_F_FATAL("util", "eventfd create error! %d:%s \n", errno, strerror(errno));
        abort();
    }
    return evfd;
}

/**
 * @brief 创建timerfd句柄
 * @return int32_t
 */
int32_t CreateTimerFd()
{
    int32_t timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(timerfd < 0)
    {
        BASE_F_FATAL("util", "timerfd_createerror! %d:%s \n", errno, strerror(errno));
        abort();
    }
    return timerfd;
}

/**
 * @brief 去除string左右所有空格
 * @param str
 */
void DelSpaceHelper(std::string &str)
{
    if(str.empty())
    {
        return;
    }
    auto pos = str.find_first_not_of(' ');
    pos = pos == std::string::npos ? 0 : pos;

    auto pos2 = str.find_last_not_of(' ');
    pos2 = pos2 == std::string::npos ? 0 : pos2;
    str = str.substr(pos, pos2 - pos + 1);
}

/**
 * @brief 生成UUID
 * @return std::string 
 */
std::string GenerateUuid()
{
    std::random_device rd;
    auto seed_data = std::array<int, std::mt19937::state_size> {};
    std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));

    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));

    std::mt19937 generator(seq);
    uuids::uuid_random_generator gen{generator};

    uuids::uuid uuid = gen();
    return uuids::to_string(uuid);
}

std::string Sha1BytesBase64Helper(const std::vector<uint8_t> &data)
{
    uuids::detail::sha1 sha;
    sha.process_bytes(data.data(), data.size());


    uuids::detail::sha1::digest8_t digest;
    sha.get_digest_bytes(digest);

    return cppcodec::base64_rfc4648::encode(digest);
}

std::string Sha1BytesBase64Helper(const std::vector<char> &data)
{
    return Sha1BytesBase64Helper(std::vector<uint8_t>(data.begin(), data.end()));
}

std::string Sha1BytesBase64Helper(const std::string &data)
{
    return Sha1BytesBase64Helper(std::vector<uint8_t>(data.begin(), data.end()));
}

bool IsUtf8Safe(const void *data, size_t size, std::string& utf8_error)
{
    const char *text = reinterpret_cast<const char*>(data);
    simdutf::result result = simdutf::validate_utf8_with_errors(text, size);
    if(simdutf::error_code::SUCCESS != result.error)
    {
        utf8_error = "invalid utf-8 at byte offset " + std::to_string(result.count);
        return false;
    }

    utf8_error.clear();
    return true;
}

std::string Utf8SafePrefix(const void *data, size_t size, size_t max_bytes)
{
    if(data == nullptr || size == 0 || max_bytes == 0)
    {
        return {};
    }

    const char *text = reinterpret_cast<const char*>(data);
    size_t prefix_size = std::min(size, max_bytes);
    while(prefix_size > 0)
    {
        simdutf::result result = simdutf::validate_utf8_with_errors(text, prefix_size);
        if(simdutf::error_code::SUCCESS == result.error)
        {
            break;
        }

        if(result.count < prefix_size)
        {
            prefix_size = result.count;
        }
        else
        {
            --prefix_size;
        }
    }

    return std::string(text, prefix_size);
}

std::string NormalizeFilePath(const std::string& file_path)
{
    // 拒绝全空白字符路径
    // "a bc.log" 中间有空白允许
    const bool contains_control_character = std::any_of(file_path.begin(),file_path.end(),
    [](auto &&ch) 
    {
        const auto value = static_cast<unsigned char>(ch);

        return value == '\t'
            || value == '\n'
            || value == '\r'
            || value == '\v'
            || value == '\f';
    });

    if(contains_control_character)
    {
        throw std::invalid_argument("file path must not contain control characters");
    }


    std::error_code error;
    auto path = std::filesystem::absolute(std::filesystem::path{file_path}, error);
    if(error)
    {
        throw std::runtime_error(
            "cannot make file path absolute: " + file_path
            + "; " + error.message());
    }

    path = std::filesystem::weakly_canonical(path, error);
    if(error)
    {
        throw std::runtime_error(
            "cannot normalize file path: " + file_path
            + "; " + error.message());
    }

    return path.string();
}

std::string Trim(const std::string &str)
{
    if(str.empty())
    {
        return str;
    }

    auto pos1 = str.find_first_not_of(' ');
    auto pos2 = str.find_last_not_of(' ');
    if(std::string::npos == pos1 && std::string::npos == pos2)
    {
        return "";
    }
    return str.substr(pos1, pos2 - pos1 + 1);
}


} // namespace kit
