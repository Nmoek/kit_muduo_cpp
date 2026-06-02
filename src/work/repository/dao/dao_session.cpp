#include "dao/dao_session.h"

#include "base/time_stamp.h"
#include "dao/dao_log.h"
#include "dao/dao_util.h"
#include "dao/sqlite_orm_pool.h"

#include <system_error>

using namespace sqlite_orm;

namespace kit_dao {

namespace {
kit_dao::UserSession NotFoundSession()
{
    kit_dao::UserSession session;
    session.m_id = -1;
    return session;
}
}

SqliteOrmSessionDao::SqliteOrmSessionDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool)
    :_db_pool(std::move(db_pool))
{
}

int64_t SqliteOrmSessionDao::Insert(kit_muduo::HttpContextPtr ctx, kit_dao::UserSession session)
{
    session.m_id = 0;
    session.m_ctime = session.m_utime = kit_muduo::TimeStamp::NowMs();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return -1;
    }

    try {
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[insert session], user_id[%ld], expire_time[%ld], secret_hash_empty[%d]\n",
                tx_result.toInt(),
                session.m_userId,
                session.m_expireTime,
                session.m_secretHash.empty());
            return -1;
        }
        int64_t session_id = tx_result.val->db().insert(session);
        tx_result.val->commit();
        return session_id;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s user_id[%ld], expire_time[%ld], secret_hash_empty[%d]\n",
            MakeSqliteErrorMsg(e, "insert session").c_str(),
            session.m_userId,
            session.m_expireTime,
            session.m_secretHash.empty());
        return -1;
    }
}

kit_dao::UserSession SqliteOrmSessionDao::GetById(kit_muduo::HttpContextPtr ctx, int64_t session_id)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return NotFoundSession();
    }

    try {
        auto session = lease_result.val->db().get_pointer<kit_dao::UserSession>(session_id);
        return session ? *session : NotFoundSession();
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s session_id[%ld]\n",
            MakeSqliteErrorMsg(e, "get session by id").c_str(),
            session_id);
        return NotFoundSession();
    }
}

bool SqliteOrmSessionDao::DeleteById(kit_muduo::HttpContextPtr ctx, int64_t session_id)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[delete session], session_id[%ld]\n",
                tx_result.toInt(),
                session_id);
            return false;
        }
        tx_result.val->db().remove_all<kit_dao::UserSession>(
            where(c(&kit_dao::UserSession::m_id) == session_id));
        tx_result.val->commit();
        return true;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s session_id[%ld]\n",
            MakeSqliteErrorMsg(e, "delete session").c_str(),
            session_id);
        return false;
    }
}

bool SqliteOrmSessionDao::DeleteByUserId(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[delete user sessions], user_id[%ld]\n",
                tx_result.toInt(),
                user_id);
            return false;
        }
        tx_result.val->db().remove_all<kit_dao::UserSession>(
            where(c(&kit_dao::UserSession::m_userId) == user_id));
        tx_result.val->commit();
        return true;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s user_id[%ld]\n",
            MakeSqliteErrorMsg(e, "delete user sessions").c_str(),
            user_id);
        return false;
    }
}

int32_t SqliteOrmSessionDao::DeleteExpired(kit_muduo::HttpContextPtr ctx, int64_t now_ms)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return -1;
    }

    try {
        int32_t count = static_cast<int32_t>(lease_result.val->db().count<kit_dao::UserSession>(
            where(c(&kit_dao::UserSession::m_expireTime) <= now_ms)));
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[delete expired sessions], now_ms[%ld], expired_count[%d]\n",
                tx_result.toInt(),
                now_ms,
                count);
            return -1;
        }
        tx_result.val->db().remove_all<kit_dao::UserSession>(
            where(c(&kit_dao::UserSession::m_expireTime) <= now_ms));
        tx_result.val->commit();
        return count;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s now_ms[%ld]\n",
            MakeSqliteErrorMsg(e, "delete expired sessions").c_str(),
            now_ms);
        return -1;
    }
}

} // namespace kit_dao
