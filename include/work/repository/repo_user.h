#ifndef __KIT_REPO_USER_H__
#define __KIT_REPO_USER_H__

#include "domain/user.h"
#include "net/call_backs.h"

#include <memory>
#include <vector>

namespace kit_dao {
class UserDaoInterface;
}

namespace kit_domain {

struct UserCandidate;

class UserRepoInterface {
public:
    explicit UserRepoInterface(std::shared_ptr<kit_dao::UserDaoInterface> dao)
        :_dao(std::move(dao))
    {
    }
    virtual ~UserRepoInterface() = default;

    virtual int64_t Create(kit_muduo::HttpContextPtr ctx, const User &user) = 0;
    virtual bool Update(kit_muduo::HttpContextPtr ctx, const User &user) = 0;
    virtual bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t user_id, UserStatus status) = 0;
    virtual bool UpdatePasswordHash(kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash) = 0;
    virtual User GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id) = 0;
    virtual User GetByNoteName(kit_muduo::HttpContextPtr ctx, const std::string &note_name) = 0;
    virtual std::vector<User> List(kit_muduo::HttpContextPtr ctx, UserStatus status, int32_t offset, int32_t limit) = 0;
    virtual int32_t CountActiveAdmin(kit_muduo::HttpContextPtr ctx) = 0;
    virtual std::vector<UserCandidate> GetNotes(kit_muduo::HttpContextPtr ctx, const std::string &keyword, int32_t limit) = 0;

protected:
    std::shared_ptr<kit_dao::UserDaoInterface> _dao;
};

class UserRepository : public UserRepoInterface {
public:
    explicit UserRepository(std::shared_ptr<kit_dao::UserDaoInterface> dao);
    ~UserRepository() = default;

    int64_t Create(kit_muduo::HttpContextPtr ctx, const User &user) override;
    bool Update(kit_muduo::HttpContextPtr ctx, const User &user) override;
    bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t user_id, UserStatus status) override;
    bool UpdatePasswordHash(kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash) override;
    User GetById(kit_muduo::HttpContextPtr ctx, int64_t user_id) override;
    User GetByNoteName(kit_muduo::HttpContextPtr ctx, const std::string &note_name) override;
    std::vector<User> List(kit_muduo::HttpContextPtr ctx, UserStatus status, int32_t offset, int32_t limit) override;
    int32_t CountActiveAdmin(kit_muduo::HttpContextPtr ctx) override;
    std::vector<UserCandidate> GetNotes(kit_muduo::HttpContextPtr ctx, const std::string &keyword, int32_t limit) override;
};

} // namespace kit_domain

#endif // __KIT_REPO_USER_H__
