/**
 * @file test_user_auth.cpp
 * @brief 用户登录、会话鉴权和用户管理服务单元测试
 */

#include <gtest/gtest.h>

#include "base/time_stamp.h"
#include "dao/session.h"
#include "net/http/http_context.h"
#include "repository/mock/repo_session_mock.h"
#include "repository/mock/repo_user_mock.h"
#include "service/password_hasher.h"
#include "service/svc_auth.h"
#include "service/svc_user.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo;

namespace {

User MissingUser()
{
    User user;
    user.id = -1;
    return user;
}

kit_dao::UserSession MissingSession()
{
    kit_dao::UserSession session;
    session.m_id = -1;
    return session;
}

int64_t ParseSessionId(const std::string &cookie_value)
{
    const auto pos = cookie_value.find('.');
    if(pos == std::string::npos)
    {
        return -1;
    }
    return std::stoll(cookie_value.substr(0, pos));
}

class UserAuthSuite : public ::testing::Test {
protected:
    void SetUp() override
    {
        user_repo_ = std::make_shared<testing::NiceMock<MockUserRepo>>();
        session_repo_ = std::make_shared<testing::NiceMock<MockSessionRepo>>();
        BindUserRepoMock();
        BindSessionRepoMock();
        auth_svc_ = std::make_unique<AuthService>(user_repo_, session_repo_);
        user_svc_ = std::make_unique<UserService>(user_repo_, session_repo_);
    }

    int64_t AddNormalUser(const std::string &note)
    {
        return user_svc_->AddUser(nullptr, UserCreateParam{note, UserRole::kNormal, ""});
    }

    int64_t AddAdminUser(const std::string &note, const std::string &password)
    {
        return user_svc_->AddUser(nullptr, UserCreateParam{note, UserRole::kAdmin, password});
    }

    void SetUserStatus(int64_t user_id, UserStatus status)
    {
        users_.at(user_id).status = status;
    }

    void ExpireSession(int64_t session_id)
    {
        sessions_.at(session_id).m_expireTime = TimeStamp::NowMs() - 1;
    }

    void BindUserRepoMock()
    {
        using testing::_;
        using testing::Invoke;

        ON_CALL(*user_repo_, Create(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, const User &user) {
                for(const auto &item : users_)
                {
                    if(item.second.note_name == user.note_name)
                    {
                        return int64_t{-1};
                    }
                }

                User saved = user;
                saved.id = next_user_id_++;
                saved.ctime = saved.utime = TimeStamp::NowMs();
                users_[saved.id] = saved;
                return saved.id;
            }));

        ON_CALL(*user_repo_, Update(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, const User &user) {
                auto iter = users_.find(user.id);
                if(iter == users_.end())
                {
                    return false;
                }
                User saved = user;
                saved.ctime = iter->second.ctime;
                saved.utime = TimeStamp::NowMs();
                iter->second = saved;
                return true;
            }));

        ON_CALL(*user_repo_, UpdateStatus(_, _, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t user_id, UserStatus status) {
                auto iter = users_.find(user_id);
                if(iter == users_.end())
                {
                    return false;
                }
                iter->second.status = status;
                iter->second.utime = TimeStamp::NowMs();
                return true;
            }));

        ON_CALL(*user_repo_, UpdatePasswordHash(_, _, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t user_id, const std::string &password_hash) {
                auto iter = users_.find(user_id);
                if(iter == users_.end())
                {
                    return false;
                }
                iter->second.password_hash = password_hash;
                iter->second.utime = TimeStamp::NowMs();
                return true;
            }));

        ON_CALL(*user_repo_, GetById(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t user_id) {
                auto iter = users_.find(user_id);
                return iter == users_.end() ? MissingUser() : iter->second;
            }));

        ON_CALL(*user_repo_, GetByNoteName(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, const std::string &note_name) {
                for(const auto &item : users_)
                {
                    if(item.second.note_name == note_name)
                    {
                        return item.second;
                    }
                }
                return MissingUser();
            }));

        ON_CALL(*user_repo_, List(_, _, _, _))
            .WillByDefault(Invoke([this](HttpContextPtr, UserStatus status, int32_t offset, int32_t limit) {
                std::vector<User> result;
                for(const auto &item : users_)
                {
                    if(status == UserStatus::kUnknown || item.second.status == status)
                    {
                        result.push_back(item.second);
                    }
                }
                if(offset < 0)
                {
                    offset = 0;
                }
                if(limit < 0)
                {
                    limit = 0;
                }
                if(offset >= static_cast<int32_t>(result.size()))
                {
                    return std::vector<User>{};
                }
                const auto begin = result.begin() + offset;
                const auto end = result.begin() + std::min<int32_t>(offset + limit, result.size());
                return std::vector<User>(begin, end);
            }));

        ON_CALL(*user_repo_, CountActiveAdmin(_))
            .WillByDefault(Invoke([this](HttpContextPtr) {
                return static_cast<int32_t>(std::count_if(users_.begin(), users_.end(),
                    [](const auto &item) {
                        return item.second.role == UserRole::kAdmin
                            && item.second.status == UserStatus::kActive;
                    }));
            }));
    }

    void BindSessionRepoMock()
    {
        using testing::_;
        using testing::Invoke;

        ON_CALL(*session_repo_, Create(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, const kit_dao::UserSession &session) {
                kit_dao::UserSession saved = session;
                saved.m_id = next_session_id_++;
                saved.m_ctime = saved.m_utime = TimeStamp::NowMs();
                sessions_[saved.m_id] = saved;
                return saved.m_id;
            }));

        ON_CALL(*session_repo_, GetById(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t session_id) {
                auto iter = sessions_.find(session_id);
                return iter == sessions_.end() ? MissingSession() : iter->second;
            }));

        ON_CALL(*session_repo_, DeleteById(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t session_id) {
                sessions_.erase(session_id);
                return true;
            }));

        ON_CALL(*session_repo_, DeleteByUserId(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t user_id) {
                for(auto iter = sessions_.begin(); iter != sessions_.end();)
                {
                    if(iter->second.m_userId == user_id)
                    {
                        iter = sessions_.erase(iter);
                    }
                    else
                    {
                        ++iter;
                    }
                }
                return true;
            }));

        ON_CALL(*session_repo_, DeleteExpired(_, _))
            .WillByDefault(Invoke([this](HttpContextPtr, int64_t now_ms) {
                int32_t count = 0;
                for(auto iter = sessions_.begin(); iter != sessions_.end();)
                {
                    if(iter->second.m_expireTime <= now_ms)
                    {
                        iter = sessions_.erase(iter);
                        ++count;
                    }
                    else
                    {
                        ++iter;
                    }
                }
                return count;
            }));
    }

    std::shared_ptr<testing::NiceMock<MockUserRepo>> user_repo_;
    std::shared_ptr<testing::NiceMock<MockSessionRepo>> session_repo_;
    std::unique_ptr<AuthService> auth_svc_;
    std::unique_ptr<UserService> user_svc_;
    std::map<int64_t, User> users_;
    std::map<int64_t, kit_dao::UserSession> sessions_;
    int64_t next_user_id_{1};
    int64_t next_session_id_{1};
};

/*
测试思路：
1. 先创建一个 active 普通用户 tester01。
2. 使用 normal 登录模式登录，拿到服务端签发的 session cookie。
3. 再用 cookie 调 AuthService::Authenticate，确认能还原 CurrentUser，并写入 HttpContext attribute。

示意：
  tester01(active normal)
          |
          v
  POST /auth/login(normal) -> kit_session=1.secret
          |
          v
  Authenticate(cookie) -> CurrentUser(user_id=tester01)

举例：
  前端普通用户只输入 note=tester01 时，后端应能创建会话；后续访问业务接口时，
  HttpContext 中应存在 auth.user_id/auth.role，供项目和协议权限校验使用。
*/
TEST_F(UserAuthSuite, NormalUserLoginCreatesSessionAndAuthenticateSetsContext)
{
    const int64_t user_id = AddNormalUser("tester01");
    ASSERT_GT(user_id, 0);

    LoginResult result = auth_svc_->Login(nullptr, LoginRequest{"tester01", "normal", ""});
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.user.user_id, user_id);
    EXPECT_EQ(result.user.role, UserRole::kNormal);
    EXPECT_FALSE(result.cookie_value.empty());

    auto ctx = std::make_shared<kit_muduo::http::HttpContext>();
    auto current_user = auth_svc_->Authenticate(ctx, result.cookie_value);
    ASSERT_TRUE(current_user.has_value());
    EXPECT_EQ(current_user->user_id, user_id);
    EXPECT_EQ(ctx->attribute("auth.user_id"), std::to_string(user_id));
    EXPECT_EQ(ctx->attribute("auth.role"), "normal");
}

/*
测试思路：
1. 分别准备一个 active 管理员 admin01 和一个 active 普通用户 tester01。
2. admin 登录模式下先传错密码，应该失败；再传正确密码，应该成功。
3. 普通用户即使知道自己的 note，也不能使用 admin 登录模式绕过角色限制。

示意：
  admin01 + wrong password -> reject
  admin01 + right password -> success
  tester01 + admin mode    -> reject

举例：
  管理员登录页面输入 admin01/adminpass 才能进入；普通用户 tester01 不能在管理员
  登录模式下留空密码或随便输入密码获得管理员入口。
*/
TEST_F(UserAuthSuite, AdminLoginRequiresAdminRoleAndCorrectPassword)
{
    ASSERT_GT(AddAdminUser("admin01", "adminpass"), 0);
    ASSERT_GT(AddNormalUser("tester01"), 0);

    LoginResult wrong_password = auth_svc_->Login(nullptr, LoginRequest{"admin01", "admin", "badpass"});
    EXPECT_FALSE(wrong_password.ok);
    EXPECT_EQ(wrong_password.message, "invalid password");

    LoginResult normal_as_admin = auth_svc_->Login(nullptr, LoginRequest{"tester01", "admin", "adminpass"});
    EXPECT_FALSE(normal_as_admin.ok);
    EXPECT_EQ(normal_as_admin.message, "invalid password");

    LoginResult ok = auth_svc_->Login(nullptr, LoginRequest{"admin01", "admin", "adminpass"});
    ASSERT_TRUE(ok.ok) << ok.message;
    EXPECT_EQ(ok.user.role, UserRole::kAdmin);
    EXPECT_FALSE(ok.cookie_value.empty());
}

/*
测试思路：
1. unknown01 不在用户表中，normal 登录应该失败。
2. disabled01 是已停用普通用户，normal 登录也应该失败。
3. bad-note 不满足 note 规则，应该在查库前失败。

示意：
  invalid note  -> invalid note
  missing note  -> invalid user
  disabled user -> invalid user

举例：
  普通用户 note 必须是 3 到 32 位字母数字；被管理员停用后，即使 note 正确也不能
  再创建新 session。
*/
TEST_F(UserAuthSuite, NormalLoginRejectsInvalidMissingAndDisabledUsers)
{
    const int64_t disabled_id = AddNormalUser("disabled01");
    ASSERT_GT(disabled_id, 0);
    ASSERT_TRUE(user_svc_->DisableUser(nullptr, disabled_id));

    LoginResult invalid_note = auth_svc_->Login(nullptr, LoginRequest{"bad-note", "normal", ""});
    EXPECT_FALSE(invalid_note.ok);
    EXPECT_EQ(invalid_note.message, "invalid note");

    LoginResult missing = auth_svc_->Login(nullptr, LoginRequest{"unknown01", "normal", ""});
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(missing.message, "invalid user");

    LoginResult disabled = auth_svc_->Login(nullptr, LoginRequest{"disabled01", "normal", ""});
    EXPECT_FALSE(disabled.ok);
    EXPECT_EQ(disabled.message, "invalid user");
}

/*
测试思路：
1. 普通用户登录成功后，手动让 session 过期，鉴权必须失败。
2. 重新登录后执行 logout，session 被删除，鉴权必须失败。
3. 再次登录后停用用户，即使 cookie 仍在，鉴权也必须失败。

示意：
  valid cookie -> expire session -> Authenticate = null
  valid cookie -> logout/delete  -> Authenticate = null
  valid cookie -> user disabled  -> Authenticate = null

举例：
  浏览器里残留旧 Cookie 时，只要服务端 session 过期、登出删除或用户被停用，
  后端统一鉴权入口都不应再放行。
*/
TEST_F(UserAuthSuite, AuthenticateRejectsExpiredDeletedAndDisabledSessions)
{
    const int64_t user_id = AddNormalUser("tester01");
    ASSERT_GT(user_id, 0);

    LoginResult expired_login = auth_svc_->Login(nullptr, LoginRequest{"tester01", "normal", ""});
    ASSERT_TRUE(expired_login.ok);
    ExpireSession(ParseSessionId(expired_login.cookie_value));
    EXPECT_FALSE(auth_svc_->Authenticate(nullptr, expired_login.cookie_value).has_value());

    LoginResult logout_login = auth_svc_->Login(nullptr, LoginRequest{"tester01", "normal", ""});
    ASSERT_TRUE(logout_login.ok);
    EXPECT_TRUE(auth_svc_->Logout(nullptr, logout_login.cookie_value));
    EXPECT_FALSE(auth_svc_->Authenticate(nullptr, logout_login.cookie_value).has_value());

    LoginResult disabled_login = auth_svc_->Login(nullptr, LoginRequest{"tester01", "normal", ""});
    ASSERT_TRUE(disabled_login.ok);
    SetUserStatus(user_id, UserStatus::kDisabled);
    EXPECT_FALSE(auth_svc_->Authenticate(nullptr, disabled_login.cookie_value).has_value());
}

/*
测试思路：
1. 空用户表启动时，如果 KIT_ADMIN_NOTE/KIT_ADMIN_PASSWORD 缺失，BootstrapAdmin 应抛异常。
2. 设置合法环境变量后再次启动，应自动创建一个 active admin。
3. 再调用一次 BootstrapAdmin，因为已有 active admin，不应重复创建。

示意：
  no env + no admin -> throw
  env ok + no admin -> create admin01
  env ok + has admin -> no-op

举例：
  首次部署时需要通过 KIT_ADMIN_NOTE=admin01、KIT_ADMIN_PASSWORD=adminpass 初始化；
  重启服务时不能重复插入 admin01。
*/
TEST_F(UserAuthSuite, BootstrapAdminRequiresEnvAndCreatesOnlyFirstAdmin)
{
    unsetenv("KIT_ADMIN_NOTE");
    unsetenv("KIT_ADMIN_PASSWORD");
    EXPECT_THROW(auth_svc_->BootstrapAdmin(nullptr), std::runtime_error);

    setenv("KIT_ADMIN_NOTE", "admin01", 1);
    setenv("KIT_ADMIN_PASSWORD", "adminpass", 1);
    EXPECT_NO_THROW(auth_svc_->BootstrapAdmin(nullptr));
    EXPECT_EQ(user_repo_->CountActiveAdmin(nullptr), 1);

    User admin = user_repo_->GetByNoteName(nullptr, "admin01");
    ASSERT_GT(admin.id, 0);
    EXPECT_EQ(admin.role, UserRole::kAdmin);
    EXPECT_EQ(admin.status, UserStatus::kActive);
    EXPECT_TRUE(PasswordHasher::Verify("adminpass", admin.password_hash));

    EXPECT_NO_THROW(auth_svc_->BootstrapAdmin(nullptr));
    EXPECT_EQ(user_repo_->CountActiveAdmin(nullptr), 1);

    unsetenv("KIT_ADMIN_NOTE");
    unsetenv("KIT_ADMIN_PASSWORD");
}

/*
测试思路：
1. 创建唯一 active admin，尝试停用或降级成 normal，都应失败。
2. 再创建第二个 active admin 后，停用第一个 admin 应成功。
3. 停用用户时，UserService 应同步删除该用户的所有 session，使其立即下线。

示意：
  admin01 only -> disable/downgrade = reject
  admin01 + admin02 -> disable admin01 = success
                           |
                           v
                     DeleteByUserId(admin01)

举例：
  管理员误操作不能把系统最后一个可登录管理员停掉；但存在 admin02 兜底时，
  admin01 可以被停用，admin01 的旧 Cookie 也会立刻失效。
*/
TEST_F(UserAuthSuite, UserServiceProtectsLastAdminAndClearsSessionsWhenDisabled)
{
    const int64_t admin01 = AddAdminUser("admin01", "adminpass");
    ASSERT_GT(admin01, 0);

    EXPECT_FALSE(user_svc_->DisableUser(nullptr, admin01));
    EXPECT_FALSE(user_svc_->UpdateUser(nullptr, admin01,
        UserUpdateParam{"admin01", UserRole::kNormal, UserStatus::kActive, ""}));
    EXPECT_EQ(user_repo_->GetById(nullptr, admin01).role, UserRole::kAdmin);
    EXPECT_EQ(user_repo_->GetById(nullptr, admin01).status, UserStatus::kActive);

    LoginResult login = auth_svc_->Login(nullptr, LoginRequest{"admin01", "admin", "adminpass"});
    ASSERT_TRUE(login.ok);
    ASSERT_EQ(sessions_.size(), 1U);

    const int64_t admin02 = AddAdminUser("admin02", "adminpass2");
    ASSERT_GT(admin02, 0);
    EXPECT_TRUE(user_svc_->DisableUser(nullptr, admin01));
    EXPECT_EQ(user_repo_->GetById(nullptr, admin01).status, UserStatus::kDisabled);
    EXPECT_TRUE(sessions_.empty());
}

} // namespace
