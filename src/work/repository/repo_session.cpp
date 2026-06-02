#include "repository/repo_session.h"

#include "dao/dao_session.h"

namespace kit_domain {

SessionRepository::SessionRepository(std::shared_ptr<kit_dao::SessionDaoInterface> dao)
    :SessionRepoInterface(std::move(dao))
{
}

int64_t SessionRepository::Create(kit_muduo::HttpContextPtr ctx, const kit_dao::UserSession &session)
{
    return _dao->Insert(ctx, session);
}

kit_dao::UserSession SessionRepository::GetById(kit_muduo::HttpContextPtr ctx, int64_t session_id)
{
    return _dao->GetById(ctx, session_id);
}

bool SessionRepository::DeleteById(kit_muduo::HttpContextPtr ctx, int64_t session_id)
{
    return _dao->DeleteById(ctx, session_id);
}

bool SessionRepository::DeleteByUserId(kit_muduo::HttpContextPtr ctx, int64_t user_id)
{
    return _dao->DeleteByUserId(ctx, user_id);
}

int32_t SessionRepository::DeleteExpired(kit_muduo::HttpContextPtr ctx, int64_t now_ms)
{
    return _dao->DeleteExpired(ctx, now_ms);
}

} // namespace kit_domain
