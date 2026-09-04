/**
 * @file util.h
 * @brief 常用工具
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-18 23:26:45
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __UTIL_H__
#define __UTIL_H__

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <sys/time.h>
#include <string>
#include <vector>

namespace kit_muduo
{

extern thread_local pid_t t_thread_id;


/**
 * @brief 获取当前进程pid
 * @return pid_t 
 */
pid_t GetPid();

/**
 * @brief 获取当前进程父进程pid
 * @return pid_t 
 */
pid_t GetPPid();

/**
 * @brief 获取内核线程pid
 * @return pid_t
 */
pid_t GetThreadPid();

/**
 * @brief 获取进程级线程tid
 * @return pthread_t
 */
pthread_t GetThreadTid();

/**
 * @brief 获取当前线程名称
 * @return std::string
 */
std::string GetThreadName();

/**
* @brief 创建eventfd句柄
* @return int32_t
*/
int32_t CreateEventFd();

/**
* @brief 创建timerfd句柄
* @return int32_t
*/
int32_t CreateTimerFd();

/**
 * @brief 去除string左右所有空格
 * @param str
 */
void DelSpaceHelper(std::string &str);

/**
 * @brief 生成UUID
 * @return std::string 
 */
std::string GenerateUuid();


/**
 * @brief 生成sha1算法加密数据后base64字符串
 * @param data 
 * @return std::string 
 */
std::string Sha1BytesBase64Helper(const std::vector<uint8_t> &data);

std::string Sha1BytesBase64Helper(const std::vector<char> &data);

std::string Sha1BytesBase64Helper(const std::string &data);

/**
 * @brief 检查文本utf-8安全
 * @param data 
 * @param size 
 * @param utf8_error 
 * @return true 
 * @return false 
 */
bool IsUtf8Safe(const void *data, size_t size, std::string& utf8_error);

/**
 * @brief 按UTF-8字符边界截取前缀
 * @param data 已确认或预期为UTF-8文本的数据
 * @param size 数据字节数
 * @param max_bytes 最大截取字节数
 * @return std::string 不会截断在UTF-8多字节字符中间
 */
std::string Utf8SafePrefix(const void *data, size_t size, size_t max_bytes);

template<typename T, typename = std::enable_if_t< std::is_arithmetic_v<T>, bool>>
bool ParsePositiveArithmetic(const std::string& value, T& out)
{
    if(value.empty())
    {
        return false;
    } 

    try {
        const char *begin = value.data();
        const char *end = begin + value.size();
        const auto& parsed = std::from_chars(begin, begin + value.size(), out);
        
        return parsed.ec == std::errc{} && parsed.ptr == end;
    } catch(const std::exception&) {
        return false;
    }
}

template<typename T, typename = std::enable_if_t< std::is_arithmetic_v<T>, bool>>
bool ToPositiveArithmetic(const T value, std::string& out)
{
    try {
        out.resize(sizeof(T));
        const char *begin = out.data();
        const char *end = begin + out.size();
        const auto& parsed = std::to_chars(begin, end, value);
        
        return parsed.ec == std::errc{} && parsed.ptr == end;
    } catch(const std::exception&) {
        return false;
    }
}

/**
 * @brief 归一化输入的文件系统路径
 * @param file_path 
 * @return std::string 
 */
std::string NormalizeFilePath(const std::string& file_path);

/**
 * @brief 去除字符串左右两边空格
 * @param str 
 * @return std::string 
 */
std::string Trim(const std::string &str);

/**
 * @brief 操作系统内存页上取整对齐
 * @param n 
 * @param page 
 * @return size_t 
 */
size_t AlignToCachePage(size_t n, size_t page);

} // namespace kit
#endif
