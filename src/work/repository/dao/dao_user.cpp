#include "dao/dao_user.h"

#include "base/time_stamp.h"
#include "dao/dao_log.h"
#include "dao/dao_util.h"
#include "dao/sqlite_orm_pool.h"
#include "dao/user.h"
#include "domain/user.h"
#include "sqlite_orm/sqlite_orm.h"

#include <system_error>
#include <algorithm>
#include <utility>

using namespace sqlite_orm;

namespace kit_dao {

namespace {
kit_dao::User NotFoundUser()
{
    kit_dao::User user;
    user.m_id = -1;
    return user;
}
}

SqliteOrmUserDao::SqliteOrmUserDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool)
    :_db_pool(std::move(db_pool))
{
}

int64_t SqliteOrmUserDao::Insert(kit_muduo::HttpContextPtr ctx, kit_dao::User user)
{
    user.m_id = 0;
    user.m_ctime = user.m_utime = kit_muduo::TimeStamp::NowMs();

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
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[insert user], note[%s], role[%d], status[%d]\n",
                tx_result.toInt(),
                user.m_noteName.c_str(),
                user.m_role,
                user.m_status);
            return -1;
        }
        int64_t user_id = tx_result.val->db().insert(user);
        tx_result.val->commit();
        return user_id;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s note[%s], role[%d], status[%d], password_hash_empty[%d]\n",
            MakeSqliteErrorMsg(e, "insert user").c_str(),
            user.m_noteName.c_str(),
            user.m_role,
            user.m_status,
            user.m_passwordHash.empty());
        return -1;
    }
}

bool SqliteOrmUserDao::Update(kit_muduo::HttpContextPtr ctx, kit_dao::User user)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        auto old_user = lease_result.val->db().get_pointer<kit_dao::User>(user.m_id);
        if(!old_user)
        {
            DAODB_F_WARN("update user target not found: user_id[%ld], note[%s], role[%d], status[%d]\n",
                user.m_id,
                user.m_noteName.c_str(),
                user.m_role,
                user.m_status);
            return false;
        }
        old_user->m_noteName = user.m_noteName;
        old_user->m_role = user.m_role;
        old_user->m_status = user.m_status;
        if(!user.m_passwordHash.empty())
        {
            old_user->m_passwordHash = user.m_passwordHash;
        }
        old_user->m_utime = kit_muduo::TimeStamp::NowMs();

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[update user], user_id[%ld], note[%s], role[%d], status[%d]\n",
                tx_result.toInt(),
                user.m_id,
                user.m_noteName.c_str(),
                user.m_role,
                user.m_status);
            return false;
        }
        tx_result.val->db().update(*old_user);
        tx_result.val->commit();
        return true;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s user_id[%ld], note[%s], role[%d], status[%d], password_hash_empty[%d]\n",
            MakeSqliteErrorMsg(e, "update user").c_str(),
            user.m_id,
            user.m_noteName.c_str(),
            user.m_role,
            user.m_status,
            user.m_passwordHash.empty());
        return false;
    }
}

bool SqliteOrmUserDao::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t user_id, int32_t status)
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
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[update user status], user_id[%ld], status[%d]\n",
                tx_result.toInt(),
                user_id,
                status);
            return false;
        }
        tx_result.val->db().update_all(
            set(c(&kit_dao::User::m_status) = status,
                c(&kit_dao::User::m_utime) = kit_muduo::TimeStamp::NowMs()),
            where(c(&kit_dao::User::m_id) == user_id));
        tx_result.val->commit();
        return true;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s user_id[%ld], status[%d]\n",
            MakeSqliteErrorMsg(e, "update user status").c_str(),
            user_id,
            status);
        return false;
    }
}

bool SqliteOrmUserDao::UpdatePasswordHash(kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash)
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
            DAODB_F_ERROR("sqlite begin write transaction error: %d, operation[update user password], user_id[%ld], password_hash_empty[%d]\n",
                tx_result.toInt(),
                user_id,
                password_hash.empty());
            return false;
        }
        tx_result.val->db().update_all(
            set(c(&kit_dao::User::m_passwordHash) = password_hash,
                c(&kit_dao::User::m_utime) = kit_muduo::TimeStamp::NowMs()),
            where(c(&kit_dao::User::m_id) == user_id));
        tx_result.val->commit();
        return true;
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s user_id[%ld], password_hash_empty[%d]\n",
            MakeSqliteErrorMsg(e, "update user password").c_str(),
            user_id,
            password_hash.empty());
        return false;
    }
}

kit_dao::User SqliteOrmUserDao::GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return NotFoundUser();
    }

    try {
        auto user = lease_result.val->db().get_pointer<kit_dao::User>(user_id);
        return user ? *user : NotFoundUser();
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s user_id[%ld]\n", MakeSqliteErrorMsg(e, "get user by id").c_str(), user_id);
        return NotFoundUser();
    }
}

kit_dao::User SqliteOrmUserDao::GetByNoteName(kit_muduo::HttpContextPtr ctx, const std::string &note_name)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return NotFoundUser();
    }

    try {
        auto users = lease_result.val->db().get_all<kit_dao::User>(
            where(c(&kit_dao::User::m_noteName) == note_name),
            limit(1));
        return users.empty() ? NotFoundUser() : users.front();
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s note[%s]\n",
            MakeSqliteErrorMsg(e, "get user by note").c_str(),
            note_name.c_str());
        return NotFoundUser();
    }
}

std::vector<kit_dao::User> SqliteOrmUserDao::List(kit_muduo::HttpContextPtr ctx, int32_t status, int32_t offset, int32_t limit_size)
{
    std::vector<kit_dao::User> users;
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return users;
    }

    try {
        if(status == 0)
        {
            users = lease_result.val->db().get_all<kit_dao::User>(
                order_by(&kit_dao::User::m_ctime).desc(),
                limit(offset, limit_size));
        }
        else
        {
            users = lease_result.val->db().get_all<kit_dao::User>(
                where(c(&kit_dao::User::m_status) == status),
                order_by(&kit_dao::User::m_ctime).desc(),
                limit(offset, limit_size));
        }
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s status[%d], offset[%d], limit[%d]\n",
            MakeSqliteErrorMsg(e, "list user").c_str(),
            status,
            offset,
            limit_size);
    }
    return users;
}

int32_t SqliteOrmUserDao::CountActiveAdmin(kit_muduo::HttpContextPtr ctx)
{
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return -1;
    }

    try {
        return static_cast<int32_t>(lease_result.val->db().count<kit_dao::User>(
            where(c(&kit_dao::User::m_role) == static_cast<int32_t>(kit_domain::UserRole::kAdmin)
                && c(&kit_dao::User::m_status) == static_cast<int32_t>(kit_domain::UserStatus::kActive))));
    } catch(const std::system_error &e) {
        DAODB_F_ERROR("%s role[%d], status[%d]\n",
            MakeSqliteErrorMsg(e, "count active admin").c_str(),
            static_cast<int32_t>(kit_domain::UserRole::kAdmin),
            static_cast<int32_t>(kit_domain::UserStatus::kActive));
        return -1;
    }
}

std::vector<kit_dao::UserCandidate> SqliteOrmUserDao::GetNotesByCondidates(kit_muduo::HttpContextPtr ctx, const std::string &keyword, int32_t limit)
{
    std::vector<kit_dao::UserCandidate> notes;
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAODB_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return {};
    }

    try {
        auto rows = lease_result.val->db().select(
            columns(
                &kit_dao::User::m_id,
                &kit_dao::User::m_noteName,
                &kit_dao::User::m_status
            )
            ,where(
                sqlite_orm::like(&kit_dao::User::m_noteName, keyword + "%")
            )
            ,order_by(&kit_dao::User::m_noteName).collate_nocase().asc()
            ,sqlite_orm::limit(std::clamp<int32_t>(limit, 1, 10))
        );

        notes.reserve(rows.size());
        for(const auto &[id, note_name, status] : rows)
        {
            notes.push_back(kit_dao::UserCandidate{
                .id = id,
                .note_name = std::move(note_name),
                .status = status
            });
        }

    } catch(const std::system_error &e) {
        DAODB_F_ERROR(
            "%s keyword[%s], limit[%d]\n",
            MakeSqliteErrorMsg(e, "select users by note name").c_str(),
            keyword.c_str(),
            limit);
        return {};
    }
    return notes;
}



} // namespace kit_dao
