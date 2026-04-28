/**
 * @file dao_project.cpp
 * @brief dao层 测试服务
 * @author ljk5
 * @version 1.0
 * @date 2025-07-23 10:50:01
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "dao/dao_project.h"
#include "dao/dao_log.h"
#include "base/time_stamp.h"

#include <thread>

namespace kit_domain {

int64_t SqliteOrmProjectDao::Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Project daoPj)
{
    daoPj.m_id = 0;  // 因为是自增主键手动置0
    auto now = kit_muduo::TimeStamp::NowMs();
    daoPj.m_ctime = daoPj.m_utime = now;

    int retry = 3;
    int id = 0;
    while(retry--)
    {
        try {
            // 这个锁保护的是事务开启动作 + 写入动作
            std::lock_guard<std::mutex> lock(_writeMtx);
            // 系统设计问题 SQLite不支持高并发写 WAL模式仅支持 TPS:100 ~ 300 否则迁移Mysql/PostorgeSql
            _db->begin_immediate_transaction();
            id = _db->insert(daoPj);
            _db->commit();

            break;
        } catch (const std::system_error& code) {
            // 专门处理一下 忙状态
            if(SQLITE_BUSY == code.code().value())
            {
                DAODB_WARN() << "sqlite3 busy, retry..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            else
            {
                throw;
            }
        } catch (...) {     //其他异常抛出
            throw;
        }

    }

    DAODB_INFO() << "qliteOrmProjectDao::Insert, id= " << id << std::endl;

    return id;
}

bool SqliteOrmProjectDao::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t projectId, bool status)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    try {
        auto pj = _db->get_pointer<kit_dao::Project>(projectId);
        if(!pj)
        {
            DAOPC_F_WARN("project dont exist! id=%ld \n", projectId);
            return false;
        }

        pj->m_status = status;
        pj->m_utime = now;

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();
        _db->update(*pj);
        _db->commit();

    } catch(const std::exception& e) {
   
        DAOPC_ERROR() << "UpdateStatus faild! " << "id= " << projectId << ", " << e.what() << std::endl;

        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProjectDao::UpdateStatus "<< "id= " << projectId << std::endl;

    return true;
}

bool SqliteOrmProjectDao::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t projectId, const std::string& name)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    try {
        auto pj = _db->get_pointer<kit_dao::Project>(projectId);
        if(!pj)
        {
            DAOPC_F_WARN("project dont exist! id=%ld \n", projectId);
            return false;
        }

        pj->m_name = name;
        pj->m_utime = now;

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();
        _db->update(*pj);
        _db->commit();

    } catch(const std::exception& e) {
   
        DAOPC_ERROR() << "UpdateName faild! " << "id= " << projectId << ", " << e.what() << std::endl;

        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProjectDao::UpdateName "<< "id= " << projectId << std::endl;

    return true;
}

kit_dao::Project SqliteOrmProjectDao::GetById(kit_muduo::HttpContextPtr ctx, int64_t projectId)
{
    kit_dao::Project pj;
    pj.m_id = -1;
    try {
        DAOPJ_DEBUG() << "projectId= " << projectId <<std::endl;
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // 等同于: SELCT * FROM [表名] WHERE id == projectId && status == 1;
        auto pj_ptr = _db->get_pointer<kit_dao::Project>(projectId);

        if(!pj_ptr)
        {
            DAOPJ_INFO() << "GetById project not exist! id= " << projectId << std::endl;
            return pj;
        }

        pj = std::move(*pj_ptr);
        DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetById " << pj.m_id << std::endl;

    } catch(const std::exception &e) {
        DAOPJ_ERROR() << "GetById faild! " << e.what() << std::endl;

        pj.m_id = -1;
    }

    return pj;
}

std::vector<kit_dao::Project> SqliteOrmProjectDao::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit)
{
    std::vector<kit_dao::Project> pjs;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE m_userId 
        pjs = _db->get_all<kit_dao::Project>(
            sqlite_orm::where(
                sqlite_orm::c(&kit_dao::Project::m_userId) == userId
                && sqlite_orm::c(&kit_dao::Project::m_status) == 1
            ), 
            sqlite_orm::order_by(&kit_dao::Project::m_ctime).desc(),
            sqlite_orm::limit(offset, limit)
            );

    } catch(const std::exception &e) {
        DAOPJ_ERROR() << "GetByUser faild! " << e.what() << std::endl;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetByUser " << userId << ", " << offset << ", " << limit << ", size= " << pjs.size() << std::endl;

    return pjs;
}

std::vector<kit_dao::Project> SqliteOrmProjectDao::GetAllByStatus(kit_muduo::HttpContextPtr ctx, int32_t status)
{
    std::vector<kit_dao::Project> pjs;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE m_userId 
        pjs = _db->get_all<kit_dao::Project>(
            sqlite_orm::where(
                sqlite_orm::c(&kit_dao::Project::m_status) == status
            )
            );

    } catch(const std::exception &e) {
        DAOPJ_ERROR() << "GetAllByStatus faild! " << e.what() << std::endl;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetAllByStatus "<< ",size= " << pjs.size() << std::endl;

    return pjs;
}

std::vector<char> SqliteOrmProjectDao::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    std::vector<char> pattern_info;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT pattern_info FROM xxx WHERE project_id 
        auto res = _db->select(
            &kit_dao::Project::m_patternInfo,
            sqlite_orm::where(
                sqlite_orm::c(&kit_dao::Project::m_id) == project_id
            )
        );

        pattern_info = std::move(res[0]);

    } catch(const std::exception &e) {

        DAOPJ_ERROR() << "GetPatternInfoById faild! " << e.what() << std::endl;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetPatternInfoById success! "<< "project_id= " << project_id << ", size=" << pattern_info.size() << std::endl;

    return pattern_info;
}

bool SqliteOrmProjectDao::UpdatePatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t pattern_type, const std::vector<char> pattern_info)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT pattern_info FROM xxx WHERE project_id 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();
        _db->update_all(
            sqlite_orm::set(
                sqlite_orm::c(&kit_dao::Project::m_patternType) = pattern_type,
                sqlite_orm::c(&kit_dao::Project::m_patternInfo) = pattern_info,
                sqlite_orm::c(&kit_dao::Project::m_utime) = now
            ),
            sqlite_orm::where(
                sqlite_orm::c(&kit_dao::Project::m_id) == project_id
            )
        );
        _db->commit();

    } catch(const std::exception &e) {

        DAOPJ_ERROR() << "GetPatternInfoById faild! " << e.what() << std::endl;
        _db->rollback();
        return false;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::UpdatePatternInfo success! "<< "project_id= " << project_id << std::endl;

    return true;
}


} // namespace kit_domain
