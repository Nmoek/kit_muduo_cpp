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

#include <cstdint>
#include <sys/time.h>
#include <string>

namespace kit_muduo
{

extern thread_local pid_t t_thread_id;

/**
 * @brief 获取ms级系统时间
 * @return uint64_t
 */
uint64_t GetTimeStampMs();

/**
 * @brief 获取us级系统时间
 * @return uint64_t
 */
uint64_t GetCurrentUs();

/**
 * @brief 时间秒数转字符串
 * @param[in] ts
 * @param[in] format
 * @return std::string
 */
std::string Timer2Str(time_t ts, const std::string& format);

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


} // namespace kit
#endif