/**
 * @file dao_project.h
 * @brief  dao层测试接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-23 10:50:20
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DAO_PROJECT_H__
#define __KIT_DAO_PROJECT_H__

#include "net/call_backs.h"
#include "dao/init.h"

#include <memory>
#include <mutex>
#include <vector>

namespace kit_domain
{


class ProjectDaoInterface
{
public:
    virtual ~ProjectDaoInterface() = default;

    virtual int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::Project daoPj) = 0;

    virtual bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, bool status) = 0;

    virtual bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name) = 0;

    virtual kit_dao::Project GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId) = 0;
    
    virtual std::vector<kit_dao::Project> GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit) = 0;

    virtual std::vector<kit_dao::Project> GetAllByStatus(kit_muduo::HttpContextPtr ctx, int32_t status) = 0;

    virtual std::vector<char> GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual bool UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info) = 0;

};

class SqliteOrmProjectDao : public ProjectDaoInterface
{
public:
    SqliteOrmProjectDao(std::shared_ptr<kit_dao::SqliteOrmType> db)
        :_db(db)
    {

    }
    ~SqliteOrmProjectDao() = default;

    int64_t Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Project daoPj) override;


    bool UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, bool status) override;

    bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name) override;

    kit_dao::Project GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId) override;

    std::vector<kit_dao::Project> GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit) override;

    std::vector<kit_dao::Project> GetAllByStatus(kit_muduo::HttpContextPtr ctx, int32_t status) override;

    std::vector<char> GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    
    bool UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info) override;


private:
    std::shared_ptr<kit_dao::SqliteOrmType> _db;
    std::mutex _writeMtx;
};


} // namespace kit_domain
#endif // __KIT_DAO_PROJECT_H__