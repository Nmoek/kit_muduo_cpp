#ifndef __KIT_REPO_SESSION_H__
#define __KIT_REPO_SESSION_H__

#include "dao/session.h"
#include "net/call_backs.h"

#include <memory>

namespace kit_dao {
class SessionDaoInterface;
}

namespace kit_domain {

class SessionRepoInterface {
public:
    explicit SessionRepoInterface(std::shared_ptr<kit_dao::SessionDaoInterface> dao)
        :_dao(std::move(dao))
    {
    }
    virtual ~SessionRepoInterface() = default;

    virtual int64_t Create(kit_muduo::HttpContextPtr ctx, const kit_dao::UserSession &session) = 0;
    virtual kit_dao::UserSession GetById(kit_muduo::HttpContextPtr ctx, int64_t session_id) = 0;
    virtual bool DeleteById(kit_muduo::HttpContextPtr ctx, int64_t session_id) = 0;
    virtual bool DeleteByUserId(kit_muduo::HttpContextPtr ctx, int64_t user_id) = 0;
    virtual int32_t DeleteExpired(kit_muduo::HttpContextPtr ctx, int64_t now_ms) = 0;

protected:
    std::shared_ptr<kit_dao::SessionDaoInterface> _dao;
};

class SessionRepository : public SessionRepoInterface {
public:
    explicit SessionRepository(std::shared_ptr<kit_dao::SessionDaoInterface> dao);
    ~SessionRepository() = default;

    int64_t Create(kit_muduo::HttpContextPtr ctx, const kit_dao::UserSession &session) override;
    kit_dao::UserSession GetById(kit_muduo::HttpContextPtr ctx, int64_t session_id) override;
    bool DeleteById(kit_muduo::HttpContextPtr ctx, int64_t session_id) override;
    bool DeleteByUserId(kit_muduo::HttpContextPtr ctx, int64_t user_id) override;
    int32_t DeleteExpired(kit_muduo::HttpContextPtr ctx, int64_t now_ms) override;
};

} // namespace kit_domain

#endif // __KIT_REPO_SESSION_H__
