#ifndef __KIT_DAO_USER_DAO_H__
#define __KIT_DAO_USER_DAO_H__

#include "dao/init.h"
#include "net/call_backs.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kit_dao {

class UserDaoInterface {
public:
    virtual ~UserDaoInterface() = default;

    virtual int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::User user) = 0;
    virtual bool Update(kit_muduo::HttpContextPtr ctx, kit_dao::User user) = 0;
    virtual bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t user_id, int32_t status) = 0;
    virtual bool UpdatePasswordHash(kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash) = 0;
    virtual kit_dao::User GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id) = 0;
    virtual kit_dao::User GetByNoteName(kit_muduo::HttpContextPtr ctx, const std::string &note_name) = 0;
    virtual std::vector<kit_dao::User> List(kit_muduo::HttpContextPtr ctx, int32_t status, int32_t offset, int32_t limit) = 0;
    virtual int32_t CountActiveAdmin(kit_muduo::HttpContextPtr ctx) = 0;
    virtual std::vector<kit_dao::UserCandidate> GetNotesByCondidates(kit_muduo::HttpContextPtr ctx, const std::string &keyword, int32_t limit) = 0;
};

class SqliteOrmUserDao : public UserDaoInterface {
public:
    explicit SqliteOrmUserDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool);
    ~SqliteOrmUserDao() = default;

    int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::User user) override;
    bool Update(kit_muduo::HttpContextPtr ctx, kit_dao::User user) override;
    bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t user_id, int32_t status) override;
    bool UpdatePasswordHash(kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash) override;
    kit_dao::User GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id) override;
    kit_dao::User GetByNoteName(kit_muduo::HttpContextPtr ctx, const std::string &note_name) override;
    std::vector<kit_dao::User> List(kit_muduo::HttpContextPtr ctx, int32_t status, int32_t offset, int32_t limit) override;
    int32_t CountActiveAdmin(kit_muduo::HttpContextPtr ctx) override;
    std::vector<kit_dao::UserCandidate> GetNotesByCondidates(kit_muduo::HttpContextPtr ctx, const std::string &keyword, int32_t limit) override;

private:
    std::shared_ptr<kit_dao::SqliteOrmPool> _db_pool;
};

} // namespace kit_dao

#endif // __KIT_DAO_USER_DAO_H__
