/**
 * @file test_asan.cpp
 * @brief ASan/LSan工具链烟雾测试
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 02:55:52
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/buffer.h"
#include "./test_log.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <unistd.h>

using namespace kit_muduo;

/**
 * 测试思路：
 * 1. 自动测试只验证 ASan 构建下基础分配/释放路径能正常执行。
 * 2. 不能在自动用例里故意泄漏或无限 sleep，否则全量测试会超时。
 * 3. 真正的泄漏探针保留在 DISABLED_ManualLeakProbe，手动排查时再显式打开。
 *
 * 示例：
 *
 *   malloc(100) -> ASSERT_NE -> free -> 用例结束
 */
TEST(TestASan, RuntimeSmoke)
{
    void *a = malloc(100);
    ASSERT_NE(a, nullptr);

    TEST_INFO() << "a=" << (void*)a << ",TestASan Over! " << std::endl;
    free(a);
}

/**
 * 测试思路：
 * 1. 这是人工验证 LeakSanitizer/进程驻留排查用例，不参与默认自动测试。
 * 2. 运行时故意泄漏 100 字节并保持进程不退出，方便外部 attach 或观察工具日志。
 * 3. 需要手动执行时使用 gtest 过滤器显式选择该 DISABLED 用例。
 *
 * 示例：
 *
 *   --gtest_also_run_disabled_tests --gtest_filter=TestASan.DISABLED_ManualLeakProbe
 *        |
 *        v
 *   malloc(100) + while sleep
 */
TEST(TestASan, DISABLED_ManualLeakProbe)
{
    void *a = malloc(100);
    ASSERT_NE(a, nullptr);

    while(1) sleep(1);
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
