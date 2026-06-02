#pragma once

#include <gmock/gmock.h>
#include "work/repository/repo_session.h"

namespace kit_domain {

class MockSessionRepo : public SessionRepoInterface {
public:
    MockSessionRepo()
        : SessionRepoInterface(nullptr) {}
    virtual ~MockSessionRepo() = default;

    MOCK_METHOD(int64_t, Create, (kit_muduo::HttpContextPtr ctx, const kit_dao::UserSession &session), (override));
    MOCK_METHOD(kit_dao::UserSession, GetById, (kit_muduo::HttpContextPtr ctx, int64_t session_id), (override));
    MOCK_METHOD(bool, DeleteById, (kit_muduo::HttpContextPtr ctx, int64_t session_id), (override));
    MOCK_METHOD(bool, DeleteByUserId, (kit_muduo::HttpContextPtr ctx, int64_t user_id), (override));
    MOCK_METHOD(int32_t, DeleteExpired, (kit_muduo::HttpContextPtr ctx, int64_t now_ms), (override));
};

} // namespace kit_domain
