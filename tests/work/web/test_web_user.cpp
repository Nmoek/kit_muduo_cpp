/**
 * @file test_web_user.cpp
 * @brief UserHandler note candidate 接口单元测试
 *
 * 测试范围只覆盖 Web Handler 到 UserService/Repository Mock 的边界：
 * 请求解析、管理员权限、输入规范化和响应 JSON 字段。
 * UserRepository/DAO 的真实 SQL 行为属于其他测试层，不在本文件重复验证。
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "domain/user.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "repository/mock/repo_session_mock.h"
#include "repository/mock/repo_user_mock.h"
#include "service/svc_user.h"
#include "web/web_user.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace testing;

namespace {

using nljson = nlohmann::json;

constexpr int64_t kAdminId = 1;
constexpr int64_t kNormalUserId = 2;

static CurrentUser AdminUser()
{
    return CurrentUser{
        kAdminId,
        "admin",
        UserRole::kAdmin,
        UserStatus::kActive,
    };
}

static CurrentUser NormalUser()
{
    return CurrentUser{
        kNormalUserId,
        "normal",
        UserRole::kNormal,
        UserStatus::kActive,
    };
}

static HttpContextPtr MakeCandidateContext(const nljson &body,
                                           CurrentUser user = AdminUser())
{
    auto ctx = std::make_shared<HttpContext>();
    SetCurrentUserToContext(ctx, std::move(user));

    auto request = ctx->request();
    request->setVersion(Version::kHttp11);
    request->setMethod(HttpRequest::Method::kPost);
    request->setPath("/users/note_candidates");
    request->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));
    request->addHeader("Content-Type", "application/json");
    request->setBodyData(body.dump());
    return ctx;
}

static void ExpectJsonResponse(HttpContextPtr ctx,
                               int32_t state_code,
                               const nljson &expected)
{
    ASSERT_EQ(ctx->response()->stateCode().toInt(), state_code);
    EXPECT_EQ(nljson::parse(ctx->response()->bodyString()), expected);
}

static UserCandidate MakeCandidate(int64_t user_id,
                                   const std::string &note_name,
                                   UserStatus status)
{
    return UserCandidate{user_id, note_name, status};
}

static std::shared_ptr<UserService> MakeUserService(
    const std::shared_ptr<MockUserRepo> &user_repo)
{
    auto session_repo = std::make_shared<NiceMock<MockSessionRepo>>();
    return std::make_shared<UserService>(user_repo, session_repo);
}

/*
测试思路：
1. 管理员提交合法前缀和 limit。
2. Handler 必须调用 UserService/Repository 链路，并只返回 user_id、note_name、status。
3. enum UserStatus 必须序列化成接口约定的 active/disabled 字符串。

示例：
  admin + {keyword:"adm",limit:10}
      -> GetNotes("adm",10)
      -> [{user_id:1,note_name:"admin",status:"active"}]
*/
TEST(UserHandlerNoteCandidatesTest, AdminReturnsCandidates)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    EXPECT_CALL(*user_repo, GetNotes(_, "adm", 10))
        .WillOnce(Return(std::vector<UserCandidate>{
            MakeCandidate(1, "admin", UserStatus::kActive),
            MakeCandidate(2, "disabled-admin", UserStatus::kDisabled),
        }));

    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson{{"keyword", "adm"}, {"limit", 10}});

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", 0},
            {"message", "success"},
            {"data", nljson::array({
                nljson{
                    {"user_id", 1},
                    {"note_name", "admin"},
                    {"status", "active"},
                },
                nljson{
                    {"user_id", 2},
                    {"note_name", "disabled-admin"},
                    {"status", "disabled"},
                },
            })},
        });
}

/*
测试思路：
1. 普通用户携带合法请求访问管理员候选接口。
2. Handler 必须在 UserService/Repository 之前返回 HTTP 403。
3. 该用例保证候选接口不会因为复用 UserService 而绕过管理员入口权限。

示例：
  normal user + {keyword:"adm",limit:10} -> HTTP 403, GetNotes 不调用
*/
TEST(UserHandlerNoteCandidatesTest, NormalUserIsForbidden)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(
        nljson{{"keyword", "adm"}, {"limit", 10}},
        NormalUser());

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k403Forbidden,
        nljson{
            {"code", -403},
            {"message", "forbidden"},
            {"data", nljson::object()},
        });
}

/*
测试思路：
1. keyword 是当前 DTO 的必填字段，缺失时绑定失败。
2. Handler 应返回 request parse error，不访问 UserService/Repository。

示例：
  admin + {} -> {code:-200,message:"request parse error"}, GetNotes 不调用
*/
TEST(UserHandlerNoteCandidatesTest, MissingKeywordIsParseError)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson::object());

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", -200},
            {"message", "request parse error"},
            {"data", nljson::object()},
        });
}

/*
测试思路：
1. keyword 字段存在但值为空字符串。
2. UserService 应直接返回空数组，不访问 Repository，避免空前缀查询退化为全量扫描。

示例：
  admin + {keyword:"",limit:10} -> {code:0,data:[]}, GetNotes 不调用
*/
TEST(UserHandlerNoteCandidatesTest, EmptyKeywordReturnsEmptyWithoutRepositoryCall)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson{{"keyword", ""}, {"limit", 10}});

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", 0},
            {"message", "success"},
            {"data", nljson::array()},
        });
}

/*
测试思路：
1. limit 虽然在 DTO 中有成员初始化值，但当前 JSON 宏仍要求字段存在。
2. Handler 应返回 request parse error，不访问 UserService/Repository。

示例：
  admin + {keyword:"adm"} -> {code:-200,message:"request parse error"}
*/
TEST(UserHandlerNoteCandidatesTest, MissingLimitIsParseError)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson{{"keyword", "adm"}});

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", -200},
            {"message", "request parse error"},
            {"data", nljson::object()},
        });
}

/*
测试思路：
1. 当前 Service 不 trim keyword，原始输入会直接透传给 Repository。
2. 该测试锁定当前落地行为，避免文档和测试继续假设存在隐式规范化。

示例：
  {keyword:"  adm  ",limit:10} -> GetNotes("  adm  ",10)
*/
TEST(UserHandlerNoteCandidatesTest, PreservesKeywordWhitespace)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    EXPECT_CALL(*user_repo, GetNotes(_, "  adm  ", 10))
        .WillOnce(Return(std::vector<UserCandidate>{}));

    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson{{"keyword", "  adm  "}, {"limit", 10}});

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", 0},
            {"message", "success"},
            {"data", nljson::array()},
        });
}

/*
测试思路：
1. limit=0 会被当前 UserService 归一为 10。
2. Repository 实际收到的是归一后的 limit，接口仍返回成功。

示例：
  {keyword:"adm",limit:0} -> GetNotes("adm",10) -> success
*/
TEST(UserHandlerNoteCandidatesTest, NormalizesZeroLimitToTen)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    EXPECT_CALL(*user_repo, GetNotes(_, "adm", 10))
        .WillOnce(Return(std::vector<UserCandidate>{}));
    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson{{"keyword", "adm"}, {"limit", 0}});

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", 0},
            {"message", "success"},
            {"data", nljson::array()},
        });
}

/*
测试思路：
1. limit=21 超出 UserService 当前允许范围。
2. UserService 将其归一为 10 后再交给 Repository。

示例：
  {keyword:"adm",limit:21} -> GetNotes("adm",10) -> success
*/
TEST(UserHandlerNoteCandidatesTest, NormalizesOverMaxLimitToTen)
{
    auto user_repo = std::make_shared<StrictMock<MockUserRepo>>();
    EXPECT_CALL(*user_repo, GetNotes(_, "adm", 10))
        .WillOnce(Return(std::vector<UserCandidate>{}));
    UserHandler handler(MakeUserService(user_repo));
    auto ctx = MakeCandidateContext(nljson{{"keyword", "adm"}, {"limit", 21}});

    handler.NoteCondidates(nullptr, ctx);

    ExpectJsonResponse(ctx,
        StateCode::k200Ok,
        nljson{
            {"code", 0},
            {"message", "success"},
            {"data", nljson::array()},
        });
}

} // namespace
