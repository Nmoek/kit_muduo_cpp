/**
 * @file sqlite_orm_pool.cpp
 * @brief 
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-28 21:01:50
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "dao/dao_log.h"
#include "dao/init.h"
#include "dao/sqlite_orm_pool.h"

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace kit_dao {


SqliteOrmLease::SqliteOrmLease(SqliteOrmPool *pool, 
    size_t slot_index,
    SqliteOrmType *db,
    uint64_t token)
    :pool_(pool)
    ,slot_index_(slot_index)
    ,db_(db)
    ,token_(token)
    ,is_released_(false)
{
    
}

SqliteOrmLease::~SqliteOrmLease()
{
    release();
}


void SqliteOrmLease::release() noexcept
{
    bool expected = false;
    if(!is_released_.compare_exchange_strong(expected, true))
    {
        return;
    }

    if(pool_)
    {
        pool_->release(slot_index_, token_);
    }

}



SqliteOrmWriteTransaction::SqliteOrmWriteTransaction(SqliteOrmLeasePtr lease, std::unique_lock<std::timed_mutex> writer_lock)
    :lease_(std::move(lease))
    ,writer_lock_(std::move(writer_lock))
    ,active_(true)
{

}

SqliteOrmWriteTransaction::~SqliteOrmWriteTransaction()
{
    // 注意: 这里的语义是没提交就要回滚
    rollback();

    // 析构结束后顺带解锁 RAII
}

void SqliteOrmWriteTransaction::commit()
{
    if(!active_)
    {
        return;
    }
    lease_->db().commit();
    active_ = false;
}

void SqliteOrmWriteTransaction::rollback() noexcept
{
    if(!active_)
    {
        return;
    }

    try
    {
        lease_->db().rollback();
    }
    catch(const std::system_error &e)
    {
        const auto& ec = e.code();
        DAODB_F_ERROR("sqlite rollback error: code[%d], category[%s], message[%s], what[%s]! \n", ec.value(),
        ec.category().name(),
        ec.message().c_str(),
        e.what());
    }

    active_ = false;
}


SqliteOrmPoolResult<SqliteOrmWriteTransactionPtr> SqliteOrmWriteTransaction::Create(SqliteOrmLeasePtr lease, int64_t time_out_ms)
{
    SqliteOrmPoolResult<SqliteOrmWriteTransactionPtr> result;

    if(!lease || lease->isReleased())
    {
        result.error = SqliteOrmPoolError::kInvalidLease;
        return result;
    }

    SqliteOrmPool *pool = lease->pool();
    if(!pool || pool->isShutdown())
    {
        result.error = SqliteOrmPoolError::kStopped;
        return result;
    }

    std::unique_lock<std::timed_mutex> writer_lock(pool->writer_mtx_, std::defer_lock);
    if(time_out_ms < 0)
    {
        writer_lock.lock();
    }
    else
    {
        if(!writer_lock.try_lock_for(std::chrono::milliseconds(time_out_ms)))
        {
            result.error = SqliteOrmPoolError::kWriteBusy;
            return result;
        }
    }


    try
    {
        lease->db().begin_immediate_transaction();
    }
    catch(const std::system_error &e)
    {
        const auto& ec = e.code();
        DAODB_F_ERROR("sqlite begin transaction error: code[%d], category[%s], message[%s], what[%s]! \n", ec.value(),
        ec.category().name(),
        ec.message().c_str(),
        e.what());
        result.error = SqliteOrmPoolError::kSqliteError;
        return result;
    }


    result.val.reset(new SqliteOrmWriteTransaction(std::move(lease), std::move(writer_lock)));
    return result;
}

SqliteOrmPool::SqliteOrmPool(SqliteOrmPoolConfig config)
    :config_(std::move(config))
    ,slots_(config.capacity > 0 ? std::make_unique<Slot[]>(config.capacity): nullptr)
    ,capacity_(config.capacity)
    ,is_shutdown_(false)
    ,active_count_(0)
{
    if(capacity_ == 0 || config_.busy_timeout_ms <= 0)
    {
        throw std::invalid_argument("sqlite orm pool config invalid");
    }

    for(size_t i = 0;i < capacity_;++i)
    {
        slots_[i].db = std::make_unique<SqliteOrmType>(SQLITE_ORM_TABLE_INIT_DEF());
        InitStorageParam(*slots_[i].db, config_.sync_schema && 0 == i);
    }

}

SqliteOrmPool::~SqliteOrmPool()
{
    shutdown();
}



SqliteOrmPoolResult<SqliteOrmLeasePtr> SqliteOrmPool::acquire() noexcept
{
    SqliteOrmPoolResult<SqliteOrmLeasePtr> result;

    if(0 == capacity_ || is_shutdown_.load())
    {
        result.error = SqliteOrmPoolError::kStopped;
        return result;
    }
    

    for(size_t i = 0;i < capacity_;++i)
    {
        Slot &slot = slots_[i];
        uint64_t token = slot.token.load();

        while((token & 1U) == 0)
        {
            uint64_t busy = token + 1;
            if(slot.token.compare_exchange_weak(token, busy))
            {
                ++active_count_;

                result.val.reset(new SqliteOrmLease(this, i, slot.db.get(), busy));

                return result;
            }
        }

    }

    result.error = SqliteOrmPoolError::kExhaust;
    return result;
}

void SqliteOrmPool::shutdown() noexcept
{
    bool expected = false;
    if(!is_shutdown_.compare_exchange_strong(expected, true))
    {
        return;
    }


}

void SqliteOrmPool::release(size_t slot_index, uint64_t token) noexcept
{
    if(slot_index >= capacity_ || 0 == token)
    {
        return;
    }

    Slot &slot = slots_[slot_index];

    uint64_t expected = token;
    if(slot.token.compare_exchange_strong(expected, token + 1))
    {
        --active_count_;
    }

}

void SqliteOrmPool::InitStorageParam(SqliteOrmType &db, bool sync_schema)
{
    db.open_forever();

    db.pragma.journal_mode(sqlite_orm::journal_mode::WAL);
    db.pragma.busy_timeout(config_.busy_timeout_ms);
    db.pragma.synchronous(config_.synchronous);

    // 注意: 只有第一个句柄会调用
    if(sync_schema)
    {
        db.sync_schema(true);
        EnsureSqliteIndexes();
    }
}


}
