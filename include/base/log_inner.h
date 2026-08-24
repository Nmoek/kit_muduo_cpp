/**
 * @file log_inner.h
 * @brief 日志系统内部打印
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 02:03:15
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_INNER_H__
#define __KIT_LOG_INNER_H__

#include "base/time_stamp.h"

#include <cstdarg>
#include <iostream>

/********日志内部打印********/
#define LOG_INNER_FMT_OUT(level, file, line, fmt, ...) do{ \
    auto func = [](const char *fmt_str, ...) -> std::string { \
        char *buf = nullptr; va_list va; \
        va_start(va, fmt_str); \
        int len = vasprintf(&buf, fmt_str, va);\
        if(len < 0) { va_end(va); if(buf){ free(buf); } return ""; } \
        va_end(va);\
        std::string str(buf); free(buf); \
        return str; \
    };\
    std::cerr  << func("[%ld][LOG %s][%s #%d] " fmt, kit_muduo::TimeStamp::NowMs(), level, file, line, ##__VA_ARGS__); \
}while(0)

#define LOG_INNER_DEBUG(fmt, ...) \
    LOG_INNER_FMT_OUT("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INNER_INFO(fmt, ...) \
    LOG_INNER_FMT_OUT("INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INNER_WARN(fmt, ...) \
    LOG_INNER_FMT_OUT("WARN", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INNER_ERROR(fmt, ...) \
    LOG_INNER_FMT_OUT("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INNER_EXCPTION(fmt, ...) \
    LOG_INNER_FMT_OUT("EXCPTION", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif //__KIT_LOG_INNER_H__