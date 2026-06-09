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
#include "dao/dao_util.h"
#include "base/time_stamp.h"
#include "dao/project.h"
#include "domain/type.h"
#include "sqlite_orm/sqlite_orm.h"
#include "dao/sqlite_orm_pool.h"
#include "nlohmann/json.hpp"

#include <thread>

using nljson = nlohmann::json;
using namespace sqlite_orm;

namespace kit_dao {

SqliteOrmProjectDao::SqliteOrmProjectDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool)
    :_db_pool(db_pool)
{

}

int64_t SqliteOrmProjectDao::Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Project daoPj)
{
    daoPj.m_id = 0;  // 因为是自增主键手动置0
    auto now = kit_muduo::TimeStamp::NowMs();
    daoPj.m_ctime = daoPj.m_utime = now;

    int64_t project_id = -1;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return project_id;
    }

    try {
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAOPC_F_ERROR("sqlite begin write transaction error: %d\n", tx_result.toInt());
            return project_id;
        }

        project_id = tx_result.val->db().insert(daoPj);
        
        tx_result.val->commit();

    } catch (const std::system_error& e) {

        DAOPJ_F_ERROR(
            "%s name[%s], protocol_type[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            daoPj.m_name.c_str(),
            daoPj.m_protocolType);

        project_id = -1;
    }

    DAODB_DEBUG() << "qliteOrmProjectDao::Insert, id= " << project_id << std::endl;

    return project_id;
}

bool SqliteOrmProjectDao::UpdateStatus(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // SELECT COUNT(*) FROM `projects` WHERE `id`= ?;
        auto n = lease_result.val->db().count<kit_dao::Project>(where(
            project_id == c(&kit_dao::Project::m_id)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("project dont exist! pjId[%ld] \n", project_id);
            return false;
        }

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }

        // UPDATE Protocols SET `status`= ? WHERE id = ? 

        tx_result.val->db().update_all(
            set(
                    c(&kit_dao::Project::m_status) = status
                    ,c(&kit_dao::Project::m_utime) = now
            )
            ,where(
                c(&kit_dao::Project::m_id) == project_id
            )
        );

        tx_result.val->commit();


    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], status[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            status);

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProjectDao::UpdateStatusById "<< "id= " << project_id << std::endl;

    return true;
}

bool SqliteOrmProjectDao::UpdateRuntimeState(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t runtime_state, uint16_t listenPort)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // SELECT COUNT(*) FROM `protocols` WHERE `id`= ?;
        auto n = lease_result.val->db().count<kit_dao::Project>(where(
            c(&kit_dao::Project::m_id) == project_id
            &&  c(&kit_dao::Project::m_status) == static_cast<int32_t>(kit_domain::ProjectStatus::kValid) 
        ));
        if(0 == n)
        {
            DAOPJ_F_WARN("project dont exist! pjId[%ld] \n", project_id);
            return false;
        }


        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }
        

        tx_result.val->db().update_all(
            set(
                    c(&kit_dao::Project::m_runtimeState) = runtime_state
                    ,c(&kit_dao::Project::m_listenPort) = listenPort
                    ,c(&kit_dao::Project::m_utime) = now
            )
            ,where(
                c(&kit_dao::Project::m_id) = project_id
            )
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], runtime_state[%d], listenPort[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            runtime_state,
            listenPort);
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProjectDao::UpdateRuntimeStatus "
        << "id= " << project_id
        << ", listen_port= " << listenPort << std::endl;

    return true;
}

bool SqliteOrmProjectDao::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t project_id, const std::string& name)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // SELECT COUNT(*) FROM `protocols` WHERE `id`= ?;
        auto n = lease_result.val->db().count<kit_dao::Project>(where(
            c(&kit_dao::Project::m_id) == project_id
            && c(&kit_dao::Project::m_status) == static_cast<int32_t>(kit_domain::ProjectStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("project dont exist! pjId[%ld] \n", project_id);
            return false;
        }

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }

        // UPDATE Protocols SET `name`= ?, `utime` = ? WHERE id = ? 

        tx_result.val->db().update_all(
            set(
                c(&kit_dao::Project::m_name) = name,
                c(&kit_dao::Project::m_utime) = now
            ),
            where(c(&kit_dao::Project::m_id) == project_id)
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], name[%s] \n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            name.c_str());
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProjectDao::UpdateName "<< "id= " << project_id << std::endl;

    return true;
}

kit_dao::Project SqliteOrmProjectDao::GetById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    kit_dao::Project pj;
    pj.m_id = -1;

    
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pj;
    }

    try {

        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // 等同于: SELCT * FROM [表名] WHERE id == project_id && status == 1;
        auto pj_ptr = lease_result.val->db().get_pointer<kit_dao::Project>(project_id);
        if(!pj_ptr)
        {
            DAOPC_F_WARN("project dont exist! pjId[%ld] \n", project_id);
            return pj;
        }

        pj = std::move(*pj_ptr);

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld] \n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id);
        return pj;
    }
    DAOPC_DEBUG() << "SqliteOrmProjectDao::GetById "<< pj.m_id << ", " << project_id << std::endl;

    return pj;
}

std::vector<kit_dao::Project> SqliteOrmProjectDao::GetByUser(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t status, int32_t offset, int32_t limit)
{
    std::vector<kit_dao::Project> pjs;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pjs;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE m_userId 
        auto tmps = lease_result.val->db().get_all<kit_dao::Project>(
            where(
                c(&kit_dao::Project::m_userId) == userId
                && c(&kit_dao::Project::m_status) == status
            ), 
            order_by(&kit_dao::Project::m_ctime).desc(),
            sqlite_orm::limit(offset, limit)
        );
        if(tmps.empty())
        {
            DAOPC_F_WARN("project dont exist! userId[%ld] \n", userId);
            return pjs;
        }

        pjs.swap(tmps);

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s userId[%ld], status[%d], offset[%d], limit[%d]\n",
            MakeSqliteErrorMsg(e).c_str(),
            userId,
            status,
            offset,
            limit);
        return pjs;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetByUser " << userId << ", " << offset << ", " << limit << ", size= " << pjs.size() << std::endl;

    return pjs;
}

std::vector<kit_dao::Project> SqliteOrmProjectDao::GetAll(kit_muduo::HttpContextPtr ctx, int32_t offset, int32_t limit)
{
    std::vector<kit_dao::Project> pjs;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pjs;
    }

    try {
        auto tmps = lease_result.val->db().get_all<kit_dao::Project>(
            order_by(&kit_dao::Project::m_ctime).desc(),
            sqlite_orm::limit(offset, limit)
        );
        pjs.swap(tmps);
    } catch (const std::system_error &e) {
        DAOPC_F_ERROR("%s offset[%d], limit[%d]\n", MakeSqliteErrorMsg(e).c_str(), offset, limit);
    }

    return pjs;
}


std::vector<kit_dao::Project>  SqliteOrmProjectDao::GetAllByStatusAndRuntimeState(kit_muduo::HttpContextPtr ctx, int32_t status, int32_t runtime_state)
{
    std::vector<kit_dao::Project> pjs;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pjs;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE `status` = ? && `runtime_state` = ?
        if(-1 == runtime_state)
        {
            pjs = lease_result.val->db().get_all<kit_dao::Project>(
                where(
                    c(&kit_dao::Project::m_status) == status
                )
            ); 
        }
        else
        {
            pjs = lease_result.val->db().get_all<kit_dao::Project>(
                where(
                    c(&kit_dao::Project::m_status) == status
                    &&  c(&kit_dao::Project::m_runtimeState) == runtime_state
                )
            ); 
        }



        if(pjs.empty())
        {
            DAOPC_F_WARN("project dont exist!\n");
            return pjs;
        }


    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s status[%d]\n",
            MakeSqliteErrorMsg(e).c_str(),
            status);
        return pjs;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetAllByStatusAndRuntimeState "<< ",size= " << pjs.size() << ", status: " << status << ", runtime_state: " << runtime_state << std::endl;

    return pjs;
}


std::string SqliteOrmProjectDao::GetPatternInfoById(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    std::string pattern_info;
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pattern_info;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT pattern_info FROM xxx WHERE project_id 
        auto tmps = lease_result.val->db().select(
            &kit_dao::Project::m_patternInfo,
            where(
                c(&kit_dao::Project::m_id) == project_id
                && c(&kit_dao::Project::m_status) == static_cast<int32_t>(kit_domain::ProjectStatus::kValid)
            )
        );
        if(tmps.empty())
        {
            DAOPC_F_WARN("project dont exist! pjId[%ld] \n", project_id);
            return pattern_info;
        }
        if(tmps.size() > 1)
        {
            DAOPC_F_WARN("project not unique! pjId[%ld]: %ld \n", project_id, tmps.size());
        }
        pattern_info.swap(tmps.at(0));

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld]\n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id);
        return pattern_info;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::GetPatternInfoById success! "<< "project_id= " << project_id << ", size=" << pattern_info.size() << std::endl;

    return pattern_info;
}

bool SqliteOrmProjectDao::UpdatePatternInfoWithProtocolWithdraw(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json& pattern_info)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // SELECT COUNT(*) FROM `projects` WHERE `id`= ? && `status` = ?;
        auto n = lease_result.val->db().count<kit_dao::Project>(where(
            c(&kit_dao::Project::m_id) == project_id
            &&
            c(&kit_dao::Project::m_status) == static_cast<int32_t>(kit_domain::ProjectStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("project dont exist! pjId[%ld] \n", project_id);
            return false;
        }
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }
        // 更新格式信息
        tx_result.val->db().update_all(
            set(
                c(&Project::m_patternInfo) = pattern_info.dump(),
                c(&Project::m_utime) = now
            ),
            where(
                c(&Project::m_id) == project_id
            )
        );

        // 更新所有协议项
        tx_result.val->db().update_all(
            set(
                c(&Protocol::m_configState) = static_cast<int32_t>(kit_domain::ProtocolConfigState::kReConfig)
                ,c(&Protocol::m_reqCfg) = "{}"
                ,c(&Protocol::m_respCfg) = "{}"
                ,c(&Protocol::m_utime) = now
            ),
            where(
                c(&Protocol::m_projectId) == project_id
                && c(&Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
            )
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], pattern_info[%s] \n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            pattern_info.dump().c_str());
        return false;
    }

    DAOPJ_DEBUG() << "SqliteOrmProjectDao::UpdatePatternInfo success! "<< "project_id= " << project_id << std::endl;

    return true;
}


} // namespace kit_domain
