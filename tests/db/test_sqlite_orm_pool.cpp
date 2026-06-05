#include "dao/sqlite_orm_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *kPoolTestDirPrefix = "/tmp/kit_sqlite_orm_pool_test_";
constexpr const char *kSqliteDbFileName = "kit.sqlite";

const char *ToString(kit_dao::SqliteOrmPoolError error)
{
    switch(error)
    {
        case kit_dao::SqliteOrmPoolError::kOk:
            return "kOk";
        case kit_dao::SqliteOrmPoolError::kStopped:
            return "kStopped";
        case kit_dao::SqliteOrmPoolError::kExhaust:
            return "kExhaust";
        case kit_dao::SqliteOrmPoolError::kWriteBusy:
            return "kWriteBusy";
        case kit_dao::SqliteOrmPoolError::kInvalidLease:
            return "kInvalidLease";
        case kit_dao::SqliteOrmPoolError::kSqliteError:
            return "kSqliteError";
    }
    return "unknown";
}

void RemoveSqliteFiles(const std::string &path)
{
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

class ScopedPoolTestDir
{
public:
    explicit ScopedPoolTestDir(const std::string &name)
        : dir_(std::string(kPoolTestDirPrefix) + name)
    {
        char cwd[PATH_MAX] = {};
        if(::getcwd(cwd, sizeof(cwd)) == nullptr)
        {
            throw std::runtime_error(std::string("getcwd failed: ") + std::strerror(errno));
        }
        old_dir_ = cwd;

        RemoveSqliteFiles(DbPath());
        ::rmdir(dir_.c_str());
        if(::mkdir(dir_.c_str(), 0700) != 0 && errno != EEXIST)
        {
            throw std::runtime_error(std::string("mkdir failed: ") + std::strerror(errno));
        }

        if(::chdir(dir_.c_str()) != 0)
        {
            throw std::runtime_error(std::string("chdir failed: ") + std::strerror(errno));
        }
    }

    ~ScopedPoolTestDir()
    {
        if(!old_dir_.empty())
        {
            if(::chdir(old_dir_.c_str()) != 0)
            {
                std::fprintf(stderr, "restore cwd failed: %s\n", std::strerror(errno));
            }
        }
        RemoveSqliteFiles(DbPath());
        ::rmdir(dir_.c_str());
    }

private:
    std::string DbPath() const
    {
        return dir_ + "/" + kSqliteDbFileName;
    }

private:
    std::string dir_;
    std::string old_dir_;
};

kit_dao::SqliteOrmPoolConfig MakePoolConfig(size_t capacity)
{
    kit_dao::SqliteOrmPoolConfig config;
    config.capacity = capacity;
    config.busy_timeout_ms = 1000;
    config.synchronous = 1;
    config.sync_schema = true;
    return config;
}

void AssertOk(const kit_dao::SqliteOrmPoolResult<kit_dao::SqliteOrmLeasePtr> &result)
{
    ASSERT_TRUE(result.ok()) << "error=" << ToString(result.error);
    ASSERT_NE(result.val, nullptr);
}

kit_dao::Project MakeProject(const std::string &name)
{
    kit_dao::Project project{};
    project.m_id = -1;
    project.m_name = name;
    project.m_mode = 1;
    project.m_protocolType = 1;
    project.m_listenPort = 0;
    project.m_targetIp = "127.0.0.1:0";
    project.m_userId = 1;
    project.m_status = 1;
    project.m_runtimeState = 0;
    project.m_patternInfo = {'p', 'o', 'o', 'l'};
    project.m_ctime = 1;
    project.m_utime = 1;
    return project;
}

int64_t CountProjects(kit_dao::SqliteOrmPool &pool)
{
    auto lease_res = pool.acquire();
    EXPECT_TRUE(lease_res.ok()) << "error=" << ToString(lease_res.error);
    if(!lease_res.ok() || !lease_res.val)
    {
        return -1;
    }
    return static_cast<int64_t>(lease_res.val->db().count<kit_dao::Project>());
}

kit_dao::SqliteOrmPoolResult<kit_dao::SqliteOrmWriteTransactionPtr>
AcquireWriteTransaction(kit_dao::SqliteOrmPool &pool, int64_t time_out_ms)
{
    kit_dao::SqliteOrmPoolResult<kit_dao::SqliteOrmWriteTransactionPtr> result;

    auto lease_res = pool.acquire();
    if(!lease_res.ok())
    {
        result.error = lease_res.error;
        return result;
    }

    return kit_dao::SqliteOrmWriteTransaction::Create(lease_res.val, time_out_ms);
}

}   // namespace

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=2
 *
 *   初始状态：
 *     [slot0: free] [slot1: free] active=0
 *
 *   acquire A:
 *     [slot0: busy(A)] [slot1: free] active=1
 *
 *   release A:
 *     [slot0: free] [slot1: free] active=0
 *
 * 该用例验证基础租约语义：
 *   1. acquire 成功后 activeCount 增加；
 *   2. lease 能拿到 pool、slot 和 db；
 *   3. release 后 activeCount 归零；
 *   4. release 重复调用不应重复归还 slot。
 */
TEST(SqliteOrmPoolTest, AcquireReleaseBasic)
{
    ScopedPoolTestDir test_dir("acquire_release_basic");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(2));

    EXPECT_EQ(pool.capacity(), 2U);
    EXPECT_EQ(pool.activeCount(), 0U);

    auto lease_res = pool.acquire();
    AssertOk(lease_res);

    EXPECT_EQ(lease_res.val->pool(), &pool);
    EXPECT_LT(lease_res.val->slotIndex(), pool.capacity());
    EXPECT_FALSE(lease_res.val->isReleased());
    EXPECT_EQ(pool.activeCount(), 1U);

    lease_res.val->release();
    EXPECT_TRUE(lease_res.val->isReleased());
    EXPECT_EQ(pool.activeCount(), 0U);

    lease_res.val->release();
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=2
 *
 *   acquire A -> slot0 busy
 *   acquire B -> slot1 busy
 *   acquire C -> 没有空闲 slot，应返回 kExhaust
 *   release A
 *   acquire D -> 应能复用 A 释放出来的 slot
 *
 * 该用例验证池耗尽和释放后复用：
 *   1. 固定容量满载后不阻塞等待，直接返回 kExhaust；
 *   2. 任一租约释放后，后续 acquire 可以重新拿到 slot；
 *   3. activeCount 只统计当前有效租约。
 */
TEST(SqliteOrmPoolTest, PoolExhaustedThenReusableAfterRelease)
{
    ScopedPoolTestDir test_dir("pool_exhausted_then_reusable");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(2));

    auto first = pool.acquire();
    AssertOk(first);

    auto second = pool.acquire();
    AssertOk(second);

    auto exhausted = pool.acquire();
    ASSERT_FALSE(exhausted.ok());
    EXPECT_EQ(exhausted.error, kit_dao::SqliteOrmPoolError::kExhaust);
    EXPECT_EQ(pool.activeCount(), 2U);

    first.val->release();
    EXPECT_EQ(pool.activeCount(), 1U);

    auto reused = pool.acquire();
    AssertOk(reused);
    EXPECT_EQ(pool.activeCount(), 2U);

    second.val->release();
    reused.val->release();
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=1
 *
 *   acquire A -> token=A
 *   release A -> slot free
 *   acquire B -> token=B
 *   release A again -> 不应影响 B
 *
 * 该用例从公共 API 层验证“旧租约不能误释放新租约”的可观察行为：
 *   1. lease 自身 release 是幂等的，旧 lease 第二次 release 不会再次进入 pool；
 *   2. 新 lease 持有期间，旧 lease 的重复释放不应改变 activeCount；
 *   3. 新 lease 仍需正常释放后 activeCount 才能归零。
 */
TEST(SqliteOrmPoolTest, StaleLeaseCannotReleaseNewOwner)
{
    ScopedPoolTestDir test_dir("old_lease_repeated_release");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    auto old_lease = pool.acquire();
    AssertOk(old_lease);
    old_lease.val->release();
    EXPECT_EQ(pool.activeCount(), 0U);

    auto new_lease = pool.acquire();
    AssertOk(new_lease);
    ASSERT_EQ(pool.activeCount(), 1U);

    old_lease.val->release();
    EXPECT_EQ(pool.activeCount(), 1U);

    new_lease.val->release();
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   {
 *       acquire A -> active=1
 *   } // A 离开作用域
 *
 *   期望：
 *     lease 析构自动 release，active 回到 0。
 *
 * 该用例验证 RAII 租约释放语义，避免调用方忘记手动 release 时 slot 永久占用。
 */
TEST(SqliteOrmPoolTest, LeaseDestructorReleasesSlot)
{
    ScopedPoolTestDir test_dir("lease_destructor_releases_slot");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    {
        auto lease_res = pool.acquire();
        AssertOk(lease_res);
        EXPECT_EQ(pool.activeCount(), 1U);
    }

    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=1
 *
 *   shutdown()
 *   acquire A -> pool 已停止，应返回 kStopped
 *
 * 该用例验证停止语义：
 *   1. shutdown 后 isShutdown 为 true；
 *   2. shutdown 后不会再发放新租约；
 *   3. 错误码与池耗尽 kExhaust 区分开。
 */
TEST(SqliteOrmPoolTest, ShutdownRejectsAcquire)
{
    ScopedPoolTestDir test_dir("shutdown_rejects_acquire");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    pool.shutdown();
    EXPECT_TRUE(pool.isShutdown());

    auto lease_res = pool.acquire();
    ASSERT_FALSE(lease_res.ok());
    EXPECT_EQ(lease_res.error, kit_dao::SqliteOrmPoolError::kStopped);
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=4，8 个线程并发执行 acquire/release：
 *
 *       T1 -> acquire -> release
 *       T2 -> acquire -> release
 *       ...
 *       T8 -> acquire 可能成功，也可能因为瞬时满载返回 kExhaust
 *
 *   最终状态：
 *     [slot0: free] [slot1: free] [slot2: free] [slot3: free] active=0
 *
 * 该用例验证 slot 租约层的并发语义：
 *   1. 并发抢占下成功租约的 slotIndex 必须合法；
 *   2. 瞬时满载只能返回 kExhaust，不能返回其他错误；
 *   3. 所有线程结束后 activeCount 必须回到 0。
 */
TEST(SqliteOrmPoolTest, ConcurrentAcquireRelease)
{
    ScopedPoolTestDir test_dir("concurrent_acquire_release");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(4));

    constexpr int kThreadCount = 8;
    constexpr int kIterations = 200;

    std::atomic_bool start{false};
    std::atomic_int success_count{0};
    std::atomic_int exhaust_count{0};
    std::atomic_int unexpected_error_count{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for(int i = 0; i < kThreadCount; ++i)
    {
        threads.emplace_back([&pool,
                              &start,
                              &success_count,
                              &exhaust_count,
                              &unexpected_error_count]() {
            while(!start.load())
            {
                std::this_thread::yield();
            }

            for(int j = 0; j < kIterations; ++j)
            {
                auto lease_res = pool.acquire();
                if(lease_res.ok())
                {
                    if(!lease_res.val || lease_res.val->slotIndex() >= pool.capacity())
                    {
                        ++unexpected_error_count;
                    }
                    ++success_count;
                    continue;
                }

                if(lease_res.error == kit_dao::SqliteOrmPoolError::kExhaust)
                {
                    ++exhaust_count;
                    std::this_thread::yield();
                    continue;
                }

                ++unexpected_error_count;
            }
        });
    }

    start.store(true);

    for(auto &thread : threads)
    {
        thread.join();
    }

    EXPECT_GT(success_count.load(), 0);
    EXPECT_GE(exhaust_count.load(), 0);
    EXPECT_EQ(unexpected_error_count.load(), 0);
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   pool.acquire()
 *     -> acquire slot0
 *   SqliteOrmWriteTransaction::Create(lease)
 *     -> 获取 writer lock
 *     -> BEGIN IMMEDIATE
 *     -> insert project
 *     -> commit
 *     -> tx 析构归还 slot
 *
 *   查询：
 *     新租约读取 projects 表，应该看到 1 条提交后的记录。
 *
 * 该用例验证写事务提交语义：
 *   1. 创建成功时事务已经开始；
 *   2. commit 后数据落库；
 *   3. commit 不立即归还 slot，事务对象析构后才归还；
 *   4. 写事务必须从 SqliteOrmWriteTransaction::Create 获取；
 *   5. 不经过 ProjectDao/ProtocolDao，只直接操作 pool lease 暴露的 storage。
 */
TEST(SqliteOrmPoolTest, WriteTransactionCommit)
{
    ScopedPoolTestDir test_dir("write_transaction_commit");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    auto tx_res = AcquireWriteTransaction(pool, 3000);
    ASSERT_TRUE(tx_res.ok()) << "error=" << ToString(tx_res.error);
    ASSERT_NE(tx_res.val, nullptr);
    EXPECT_EQ(pool.activeCount(), 1U);

    const auto id = tx_res.val->db().insert(MakeProject("commit-project"));
    EXPECT_GT(id, 0);

    tx_res.val->commit();
    EXPECT_EQ(pool.activeCount(), 1U);

    tx_res.val.reset();
    EXPECT_EQ(pool.activeCount(), 0U);
    EXPECT_EQ(CountProjects(pool), 1);
}

/*
 * 测试思路：
 *
 * 示例：
 *   {
 *       pool.acquire()
 *       SqliteOrmWriteTransaction::Create(lease)
 *       insert project
 *   } // 没有 commit，tx 析构自动 rollback
 *
 *   查询：
 *     projects 表应该仍为 0 条。
 *
 * 该用例验证写事务析构回滚语义：
 *   1. 调用方忘记 commit 时不会把半成品写入数据库；
 *   2. rollback 后事务对象释放写锁和 lease；
 *   3. activeCount 最终回到 0。
 */
TEST(SqliteOrmPoolTest, WriteTransactionRollbackOnDestructor)
{
    ScopedPoolTestDir test_dir("write_transaction_rollback_on_destructor");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    {
        auto tx_res = AcquireWriteTransaction(pool, 3000);
        ASSERT_TRUE(tx_res.ok()) << "error=" << ToString(tx_res.error);
        ASSERT_NE(tx_res.val, nullptr);

        const auto id = tx_res.val->db().insert(MakeProject("rollback-project"));
        EXPECT_GT(id, 0);
        EXPECT_EQ(pool.activeCount(), 1U);
    }

    EXPECT_EQ(pool.activeCount(), 0U);
    EXPECT_EQ(CountProjects(pool), 0);
}

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=1
 *
 *   acquire lease A + Create tx A -> slot0 busy
 *   acquire B  -> pool 没有空闲 slot，应返回 kExhaust
 *   rollback A + 析构
 *   acquire C  -> slot0 已归还，应成功
 *
 * 该用例验证写事务对象必须持有 lease：
 *   1. 事务存活期间 storage slot 不能被普通读租约抢走；
 *   2. rollback 只结束事务，不破坏 lease 的 RAII 生命周期；
 *   3. 事务对象释放后 slot 才可复用。
 */
TEST(SqliteOrmPoolTest, WriteTransactionHoldsLease)
{
    ScopedPoolTestDir test_dir("write_transaction_holds_lease");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    auto tx_res = AcquireWriteTransaction(pool, 3000);
    ASSERT_TRUE(tx_res.ok()) << "error=" << ToString(tx_res.error);
    ASSERT_NE(tx_res.val, nullptr);

    auto exhausted = pool.acquire();
    ASSERT_FALSE(exhausted.ok());
    EXPECT_EQ(exhausted.error, kit_dao::SqliteOrmPoolError::kExhaust);
    EXPECT_EQ(pool.activeCount(), 1U);

    tx_res.val->rollback();
    EXPECT_EQ(pool.activeCount(), 1U);

    tx_res.val.reset();
    EXPECT_EQ(pool.activeCount(), 0U);

    auto reused = pool.acquire();
    AssertOk(reused);
    reused.val->release();
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   lease A = pool.acquire()
 *   lease A.release()
 *   SqliteOrmWriteTransaction::Create(lease A)
 *
 *   期望：
 *     Create 返回 kInvalidLease，不能在已经归还的 storage lease 上开启写事务。
 *
 * 该用例验证新的写事务入口边界：
 *   1. 写事务必须基于仍然有效的 lease 创建；
 *   2. Create 不会重新 acquire，也不会复活已释放的 lease；
 *   3. 失败后 activeCount 仍保持 0。
 */
TEST(SqliteOrmPoolTest, WriteTransactionCreateRejectsReleasedLease)
{
    ScopedPoolTestDir test_dir("write_transaction_create_rejects_released_lease");
    kit_dao::SqliteOrmPool pool(MakePoolConfig(1));

    auto lease_res = pool.acquire();
    AssertOk(lease_res);
    lease_res.val->release();
    EXPECT_EQ(pool.activeCount(), 0U);

    auto tx_res = kit_dao::SqliteOrmWriteTransaction::Create(lease_res.val, 3000);
    ASSERT_FALSE(tx_res.ok());
    EXPECT_EQ(tx_res.error, kit_dao::SqliteOrmPoolError::kInvalidLease);
    EXPECT_EQ(pool.activeCount(), 0U);
}

/*
 * 测试思路：
 *
 * 示例：
 *   capacity=2，write_lock_timeout_ms=1
 *
 *   acquire lease A + Create tx A -> 拿到 slot0 和 writer lock
 *   acquire lease B + Create tx B -> 能拿到 slot1，但 writer lock 超时，应返回 kWriteBusy
 *   tx B 失败路径 -> 自动归还 slot1
 *
 * 该用例验证写锁超时语义：
 *   1. kWriteBusy 表示当前进程内已有 writer，不是 storage slot 耗尽；
 *   2. 第二个写事务失败后不能泄漏 lease；
 *   3. 第一个写事务仍保持有效，activeCount 应为 1。
 */
TEST(SqliteOrmPoolTest, WriteTransactionBusyReleasesAcquiredLease)
{
    ScopedPoolTestDir test_dir("write_transaction_busy_releases_lease");
    auto config = MakePoolConfig(2);
    kit_dao::SqliteOrmPool pool(config);

    auto first = AcquireWriteTransaction(pool, 1);
    ASSERT_TRUE(first.ok()) << "error=" << ToString(first.error);
    ASSERT_NE(first.val, nullptr);
    EXPECT_EQ(pool.activeCount(), 1U);

    auto second = AcquireWriteTransaction(pool, 1);
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(second.error, kit_dao::SqliteOrmPoolError::kWriteBusy);
    EXPECT_EQ(pool.activeCount(), 1U);

    first.val->rollback();
    first.val.reset();
    EXPECT_EQ(pool.activeCount(), 0U);
}
