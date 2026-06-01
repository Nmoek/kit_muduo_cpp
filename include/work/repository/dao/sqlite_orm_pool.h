/**
 * @file sqlite_orm_pool.h
 * @brief SqliteOrm租赁池
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-28 20:49:59
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_SQLITE_ORM_POOL_H__
#define __KIT_SQLITE_ORM_POOL_H__

#include "base/noncopyable.h"
#include "dao/init.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

namespace kit_dao {

struct SqliteOrmPoolConfig
{
    size_t capacity{20};
    int32_t busy_timeout_ms{3000}; // 注意:这里不是pool写锁锁等待时间  是底层DB文件锁时间
    int32_t synchronous{1};       // 1=NORMAL，2=FULL。
    bool sync_schema{true};   // 只在第一个 storage 上执行 sync_schema。
};

enum class SqliteOrmPoolError 
{
    kOk = 0,
    kStopped,
    kExhaust,
    kWriteBusy,
    kInvalidLease,
    kSqliteError,
};

template<class T>
struct SqliteOrmPoolResult
{
    T val{T()};
    SqliteOrmPoolError error{SqliteOrmPoolError::kOk};

    bool ok() const { return error == SqliteOrmPoolError::kOk; }

    int32_t toInt() const { return static_cast<int32_t>(error); }
};

template<>
struct SqliteOrmPoolResult<void>
{
    SqliteOrmPoolError error{SqliteOrmPoolError::kOk};

    bool ok() const { return error == SqliteOrmPoolError::kOk; }

    int32_t toInt() const { return static_cast<int32_t>(error); }
};

class SqliteOrmPool;
class SqliteOrmWriteTransaction;


class SqliteOrmLease: kit_muduo::Noncopyable
{
public:
    ~SqliteOrmLease();

    SqliteOrmPool *pool() const noexcept { return pool_; }

    size_t slotIndex() const { return slot_index_; }

    SqliteOrmType& db() const { return *db_; }

    SqliteOrmType& operator*() const noexcept { return *db_; }
    const SqliteOrmType *operator->() const { return db_; }

    uint64_t token() const { return token_; }

    bool isReleased() const { return is_released_.load(); }


    void release() noexcept;


private:
    friend class SqliteOrmPool;

    SqliteOrmLease(SqliteOrmPool *pool, 
        size_t slot_index,
        SqliteOrmType *db,
        uint64_t token);

private:
    SqliteOrmPool *pool_{nullptr};
    size_t slot_index_;
    SqliteOrmType *db_;
    uint64_t token_;
    std::atomic_bool is_released_{false};
};


using SqliteOrmLeasePtr = std::shared_ptr<SqliteOrmLease>;
using SqliteOrmWriteTransactionPtr = std::shared_ptr<SqliteOrmWriteTransaction>;


class SqliteOrmWriteTransaction: kit_muduo::Noncopyable
{
public:
    ~SqliteOrmWriteTransaction();

    SqliteOrmType& db() const noexcept { return lease_->db(); }

    SqliteOrmLeasePtr lease() const noexcept { return lease_; }

    void commit();
    void rollback() noexcept;

public:
    static SqliteOrmPoolResult<SqliteOrmWriteTransactionPtr> Create(SqliteOrmLeasePtr lease, int64_t time_out_ms = -1);


private:
    friend class SqliteOrmPool;

    SqliteOrmWriteTransaction(SqliteOrmLeasePtr lease, std::unique_lock<std::timed_mutex> writer_lock);

private:
    SqliteOrmLeasePtr lease_;
    std::unique_lock<std::timed_mutex> writer_lock_;
    bool active_{false}; // 表示是否已经提交/回滚
};



class SqliteOrmPool: kit_muduo::Noncopyable
{
public:
    explicit SqliteOrmPool(SqliteOrmPoolConfig config = SqliteOrmPoolConfig());

    ~SqliteOrmPool();

    SqliteOrmPoolResult<SqliteOrmLeasePtr> acquire() noexcept;

    void shutdown() noexcept;

    const SqliteOrmPoolConfig& config() const noexcept { return config_; }

    size_t capacity() const noexcept { return capacity_; }

    size_t activeCount() const noexcept { return active_count_.load(); }

    bool isShutdown() const noexcept { return is_shutdown_.load(); }

private:
    friend class SqliteOrmLease;
    friend class SqliteOrmWriteTransaction;

    void release(size_t slot_index, uint64_t token) noexcept;
    
    void InitStorageParam(SqliteOrmType &db, bool sync_schema);

private:
    struct Slot
    {
        std::unique_ptr<SqliteOrmType> db;
        std::atomic_uint64_t token{0};
    };
    SqliteOrmPoolConfig config_;
    std::unique_ptr<Slot[]> slots_;
    size_t capacity_{0};
    std::atomic_bool is_shutdown_{false};
    std::atomic_size_t active_count_{0};

    std::timed_mutex writer_mtx_;
};






}
#endif //__KIT_SQLITE_ORM_POOL_H__