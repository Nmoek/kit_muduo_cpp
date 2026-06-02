#ifndef __KIT_DAO_SESSION_DAO_H__
#define __KIT_DAO_SESSION_DAO_H__

#include "dao/init.h"
#include "net/call_backs.h"

#include <cstdint>
#include <memory>

namespace kit_dao {

class SessionDaoInterface {
public:
    virtual ~SessionDaoInterface() = default;

    virtual int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::UserSession session) = 0;
    virtual kit_dao::UserSession GetById(kit_muduo::HttpContextPtr ctx, int64_t session_id) = 0;
    virtual bool DeleteById(kit_muduo::HttpContextPtr ctx, int64_t session_id) = 0;
    virtual bool DeleteByUserId(kit_muduo::HttpContextPtr ctx, int64_t user_id) = 0;
    virtual int32_t DeleteExpired(kit_muduo::HttpContextPtr ctx, int64_t now_ms) = 0;
};

class SqliteOrmSessionDao : public SessionDaoInterface {
public:
    explicit SqliteOrmSessionDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool);
    ~SqliteOrmSessionDao() = default;

    int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::UserSession session) override;
    kit_dao::UserSession GetById(kit_muduo::HttpContextPtr ctx, int64_t session_id) override;
    bool DeleteById(kit_muduo::HttpContextPtr ctx, int64_t session_id) override;
    bool DeleteByUserId(kit_muduo::HttpContextPtr ctx, int64_t user_id) override;
    int32_t DeleteExpired(kit_muduo::HttpContextPtr ctx, int64_t now_ms) override;

private:
    std::shared_ptr<kit_dao::SqliteOrmPool> _db_pool;
};

} // namespace kit_dao

#endif // __KIT_DAO_SESSION_DAO_H__
