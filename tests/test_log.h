/**
 * @file test_log.h
 * @brief 测试统一日志器定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 22:06:18
 * @copyright Copyright (c) 2025 Kewin Li
 */

 #include "base/log.h"

static auto g_test_logger = KIT_LOGGER("test");

#define TEST_MODULE_NAME    "null"
#define TEST_DEBUG() \
    KIT_DEBUG(g_test_logger, TEST_MODULE_NAME)
#define TEST_INFO() \
    KIT_INFO(g_test_logger, TEST_MODULE_NAME)
#define TEST_WARN() \
    KIT_WARN(g_test_logger, TEST_MODULE_NAME)
#define TEST_ERROR() \
    KIT_ERROR(g_test_logger, TEST_MODULE_NAME)
#define TEST_FATAL() \
    KIT_FATAL(g_test_logger, TEST_MODULE_NAME)
