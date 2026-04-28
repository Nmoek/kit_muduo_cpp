/**
 * @file test_log.h
 * @brief 测试统一日志器定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 22:06:18
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log.h"


#define TEST_DEBUG() \
    KIT_DEBUG(KIT_LOGGER("test"), "null")
#define TEST_INFO() \
    KIT_INFO(KIT_LOGGER("test"), "null")
#define TEST_WARN() \
    KIT_WARN(KIT_LOGGER("test"), "null")
#define TEST_ERROR() \
    KIT_ERROR(KIT_LOGGER("test"), "null")
#define TEST_FATAL() \
    KIT_FATAL(KIT_LOGGER("test"), "null")

#define TEST_F_DEBUG(fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("test"), "null", fmt, ##__VA_ARGS__)

#define TEST_F_INFO(fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("test"), "null", fmt, ##__VA_ARGS__)

#define TEST_F_WARN(fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("test"), "null", fmt, ##__VA_ARGS__)

#define TEST_F_ERROR(fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("test"), "null", fmt, ##__VA_ARGS__)

#define TEST_F_FATAL(fmt, ...)  KIT_FMT_FATAL(KIT_LOGGER("test"), "null", fmt, ##__VA_ARGS__)