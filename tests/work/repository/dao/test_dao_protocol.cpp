/**
 * @file test_dao_protocol.cpp
 * @brief 协议项 DAO 分页与软删除筛选测试
 */

#include "dao/dao_protocol.h"
#include "dao/sqlite_orm_pool.h"
#include "domain/type.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace kit_domain;

namespace {

constexpr const char *kTestDir = "/tmp/kit_dao_protocol_list_test";
constexpr const char *kDbFile = "kit.sqlite";

void RemoveSqliteFiles(const std::string &path)
{
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

class ScopedDaoTestDir
{
public:
    ScopedDaoTestDir()
    {
        char cwd[PATH_MAX] = {};
        if(::getcwd(cwd, sizeof(cwd)) == nullptr)
        {
            throw std::runtime_error(std::string("getcwd failed: ") + std::strerror(errno));
        }
        old_dir_ = cwd;

        RemoveSqliteFiles(DbPath());
        ::rmdir(kTestDir);
        if(::mkdir(kTestDir, 0700) != 0 && errno != EEXIST)
        {
            throw std::runtime_error(std::string("mkdir failed: ") + std::strerror(errno));
        }
        if(::chdir(kTestDir) != 0)
        {
            throw std::runtime_error(std::string("chdir failed: ") + std::strerror(errno));
        }
    }

    ~ScopedDaoTestDir()
    {
        if(!old_dir_.empty() && ::chdir(old_dir_.c_str()) != 0)
        {
            std::fprintf(stderr, "restore cwd failed: %s\n", std::strerror(errno));
        }
        RemoveSqliteFiles(DbPath());
        ::rmdir(kTestDir);
    }

private:
    std::string DbPath() const
    {
        return std::string(kTestDir) + "/" + kDbFile;
    }

    std::string old_dir_;
};

kit_dao::SqliteOrmPoolConfig MakePoolConfig()
{
    kit_dao::SqliteOrmPoolConfig config;
    config.pool_capacity = 1;
    config.busy_timeout_ms = 1000;
    config.synchronous = 1;
    config.sync_schema = true;
    return config;
}

kit_dao::Protocol MakeProtocol(int64_t project_id,
                               ProtocolStatus status,
                               int64_t ctime,
                               const std::string &name)
{
    kit_dao::Protocol protocol{};
    protocol.m_name = name;
    protocol.m_type = static_cast<int32_t>(ProtocolType::kHttp);
    protocol.m_projectId = project_id;
    protocol.m_runtimeKey = name;
    protocol.m_status = static_cast<int32_t>(status);
    protocol.m_configState = static_cast<int32_t>(ProtocolConfigState::kOff);
    protocol.m_reqBodyType = static_cast<int32_t>(ProtocolBodyType::kJson);
    protocol.m_respBodyType = static_cast<int32_t>(ProtocolBodyType::kJson);
    protocol.m_reqBodyDataStatus = 0;
    protocol.m_respBodyDataStatus = 0;
    protocol.m_reqCfg = "{}";
    protocol.m_respCfg = "{}";
    protocol.m_isEndian = 0;
    protocol.m_ctime = ctime;
    protocol.m_utime = ctime;
    return protocol;
}

int64_t InsertProtocol(kit_dao::SqliteOrmPool &pool, kit_dao::Protocol protocol)
{
    auto lease_result = pool.acquire();
    if(!lease_result.ok() || !lease_result.val)
    {
        return -1;
    }

    auto tx_result = kit_dao::SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
    if(!tx_result.ok() || !tx_result.val)
    {
        return -1;
    }

    const auto id = tx_result.val->db().insert(protocol);
    tx_result.val->commit();
    return id;
}

void ExpectProtocolIds(const std::vector<kit_dao::Protocol> &protocols,
                       const std::vector<int64_t> &expected_ids)
{
    ASSERT_EQ(protocols.size(), expected_ids.size());
    for(size_t i = 0; i < expected_ids.size(); ++i)
    {
        EXPECT_EQ(protocols[i].m_id, expected_ids[i]);
    }
}

} // namespace

/*
测试思路：
1. 在同一 project 中插入 2 个有效项和 2 个软删除项，并额外插入另一个 project 的协议项。
2. 用 status=nullopt 分两页查询，断言 count 使用统一状态集合、每页不超过 limit、固定排序为 ctime DESC 再 id DESC，且两页没有重复项。
3. 再分别用 status=kValid/kInvalid 查询，确认状态筛选、total 和分页结果都与真实数据库一致；offset 超出范围时 items 为空但 total 保留。
4. 先查询空 project，再查询已有 project，验证空结果不会破坏后续租约/事务使用。

示例：
  project=9601: valid@300(id=1), deleted@300(id=2), valid@200(id=3), deleted@100(id=4)
      page(offset=0,limit=2) -> [id=2, id=1], total=4
      page(offset=2,limit=2) -> [id=3, id=4], total=4

该用例直接对应验收标准：统一查询、准确 total、稳定排序、跨页不重复以及软删除状态可筛选。
*/
TEST(ProtocolDaoTest, ListByProjectPaginatesUnifiedStatusSet)
{
    ScopedDaoTestDir test_dir;
    auto pool = std::make_shared<kit_dao::SqliteOrmPool>(MakePoolConfig());
    kit_dao::SqliteOrmProtocolDao dao(pool);

    constexpr int64_t project_id = 9601;
    constexpr int64_t other_project_id = 9602;
    const auto valid_new_id = InsertProtocol(
        *pool,
        MakeProtocol(project_id, ProtocolStatus::kValid, 300, "valid-new"));
    const auto deleted_new_id = InsertProtocol(
        *pool,
        MakeProtocol(project_id, ProtocolStatus::kInvalid, 300, "deleted-new"));
    const auto valid_old_id = InsertProtocol(
        *pool,
        MakeProtocol(project_id, ProtocolStatus::kValid, 200, "valid-old"));
    const auto deleted_old_id = InsertProtocol(
        *pool,
        MakeProtocol(project_id, ProtocolStatus::kInvalid, 100, "deleted-old"));
    ASSERT_GT(valid_new_id, 0);
    ASSERT_GT(deleted_new_id, 0);
    ASSERT_GT(valid_old_id, 0);
    ASSERT_GT(deleted_old_id, 0);
    ASSERT_GT(InsertProtocol(
        *pool,
        MakeProtocol(other_project_id, ProtocolStatus::kValid, 400, "other-project")), 0);

    const auto first_page = dao.ListByProject(nullptr, project_id, std::nullopt, 0, 2);
    ASSERT_EQ(first_page.second, 4);
    ASSERT_EQ(first_page.first.size(), 2U);
    ExpectProtocolIds(first_page.first, {deleted_new_id, valid_new_id});

    const auto second_page = dao.ListByProject(nullptr, project_id, std::nullopt, 2, 2);
    ASSERT_EQ(second_page.second, 4);
    ASSERT_EQ(second_page.first.size(), 2U);
    ExpectProtocolIds(second_page.first, {valid_old_id, deleted_old_id});

    std::set<int64_t> paged_ids;
    for(const auto &protocol : first_page.first)
    {
        paged_ids.insert(protocol.m_id);
    }
    for(const auto &protocol : second_page.first)
    {
        paged_ids.insert(protocol.m_id);
    }
    const std::set<int64_t> expected_paged_ids{
        valid_new_id, deleted_new_id, valid_old_id, deleted_old_id};
    EXPECT_EQ(paged_ids, expected_paged_ids);

    const auto valid_page = dao.ListByProject(
        nullptr,
        project_id,
        std::optional<int32_t>{static_cast<int32_t>(ProtocolStatus::kValid)},
        0,
        10);
    ASSERT_EQ(valid_page.second, 2);
    ASSERT_EQ(valid_page.first.size(), 2U);
    ExpectProtocolIds(valid_page.first, {valid_new_id, valid_old_id});
    for(const auto &protocol : valid_page.first)
    {
        EXPECT_EQ(protocol.m_status, static_cast<int32_t>(ProtocolStatus::kValid));
    }

    const auto deleted_page = dao.ListByProject(
        nullptr,
        project_id,
        std::optional<int32_t>{static_cast<int32_t>(ProtocolStatus::kInvalid)},
        0,
        10);
    ASSERT_EQ(deleted_page.second, 2);
    ASSERT_EQ(deleted_page.first.size(), 2U);
    ExpectProtocolIds(deleted_page.first, {deleted_new_id, deleted_old_id});
    for(const auto &protocol : deleted_page.first)
    {
        EXPECT_EQ(protocol.m_status, static_cast<int32_t>(ProtocolStatus::kInvalid));
    }

    const auto out_of_range_page = dao.ListByProject(nullptr, project_id, std::nullopt, 99, 2);
    EXPECT_TRUE(out_of_range_page.first.empty());
    EXPECT_EQ(out_of_range_page.second, 4);

    const auto empty_page = dao.ListByProject(nullptr, 9699, std::nullopt, 0, 2);
    EXPECT_TRUE(empty_page.first.empty());
    EXPECT_EQ(empty_page.second, 0);

    const auto page_after_empty = dao.ListByProject(nullptr, project_id, std::nullopt, 0, 2);
    EXPECT_EQ(page_after_empty.second, 4);
    EXPECT_EQ(page_after_empty.first.size(), 2U);
}
