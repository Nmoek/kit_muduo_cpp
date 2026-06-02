#pragma once

#include <gmock/gmock.h>
#include "work/repository/repo_user.h"

namespace kit_domain {

class MockUserRepo : public UserRepoInterface {
public:
    MockUserRepo()
        : UserRepoInterface(nullptr) {}
    virtual ~MockUserRepo() = default;

    MOCK_METHOD(int64_t, Create, (kit_muduo::HttpContextPtr ctx, const User &user), (override));
    MOCK_METHOD(bool, Update, (kit_muduo::HttpContextPtr ctx, const User &user), (override));
    MOCK_METHOD(bool, UpdateStatus, (kit_muduo::HttpContextPtr ctx, int64_t user_id, UserStatus status), (override));
    MOCK_METHOD(bool, UpdatePasswordHash, (kit_muduo::HttpContextPtr ctx, int64_t user_id, const std::string &password_hash), (override));
    MOCK_METHOD(User, GetById, (kit_muduo::HttpContextPtr ctx, int64_t user_id), (override));
    MOCK_METHOD(User, GetByNoteName, (kit_muduo::HttpContextPtr ctx, const std::string &note_name), (override));
    MOCK_METHOD(std::vector<User>, List, (kit_muduo::HttpContextPtr ctx, UserStatus status, int32_t offset, int32_t limit), (override));
    MOCK_METHOD(int32_t, CountActiveAdmin, (kit_muduo::HttpContextPtr ctx), (override));
};

} // namespace kit_domain
