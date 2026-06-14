/**
 * @file dao_protocol.cpp
 * @brief 协议项 dao层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-29 16:40:22
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "dao/dao_protocol.h"
#include "dao/protocol.h"
#include "domain/protocol.h"
#include "dao/dao_log.h"
#include "dao/dao_util.h"
#include "base/time_stamp.h"
#include "dao/sqlite_orm_pool.h"
#include "domain/type.h"

#include <exception>
#include <system_error>
#include <thread>

using nljson = nlohmann::json;
using namespace sqlite_orm;

namespace kit_dao {

SqliteOrmProtocolDao::SqliteOrmProtocolDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool)
    :_db_pool(db_pool)
{

}

int64_t SqliteOrmProtocolDao::Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Protocol daoPc)
{
    daoPc.m_id = 0;
    auto now = kit_muduo::TimeStamp::NowMs();
    daoPc.m_ctime = daoPc.m_utime = now;
    int64_t protocol_id = -1;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return protocol_id;
    }

    try {

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            DAOPC_F_ERROR("sqlite begin write transaction error: %d\n", tx_result.toInt());
            return protocol_id;
        }
        // 系统设计问题 SQLite不支持高并发写 WAL模式仅支持 TPS:100 ~ 300 否则迁移Mysql/PostorgeSql
        protocol_id = tx_result.val->db().insert(daoPc);

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s name[%s], type[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            daoPc.m_name.c_str(),
            daoPc.m_type);

        protocol_id = -1;
    }


    DAODB_INFO() << "qliteOrmProtocolDao::Insert, id= " << protocol_id << std::endl;

    return protocol_id;
}

bool SqliteOrmProtocolDao::UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t status)
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
        auto n = lease_result.val->db().count<kit_dao::Protocol>(where(
            protocol_id == c(&kit_dao::Protocol::m_id)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return false;
        }


        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }


        // UPDATE Protocols SET `status`= ?, `utime` = ? WHERE id = ?

        tx_result.val->db().update_all(
            set(
                    c(&kit_dao::Protocol::m_status) = status,
                    c(&kit_dao::Protocol::m_utime) = now
            ),
            where(c(&kit_dao::Protocol::m_id) == protocol_id)
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], status[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            status);

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateStatusById "<< "id= " << protocol_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateById(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc)
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
        auto n = lease_result.val->db().count<Protocol>(where(
            c(&Protocol::m_id) == daoPc.m_id
            && c(&Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", daoPc.m_id);
            return false;
        }

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }

        // UPDATE Protocols SET ... WHERE id = ?
        // 注意: 这里更新时 id主键、type、ctime不更新
        tx_result.val->db().update_all(
            set(
                c(&Protocol::m_name) = daoPc.m_name
                ,c(&Protocol::m_projectId) = daoPc.m_projectId
                ,c(&Protocol::m_runtimeKey) = daoPc.m_runtimeKey            
                ,c(&Protocol::m_configState) = daoPc.m_configState
                ,c(&Protocol::m_reqBodyType) = daoPc.m_reqBodyType 
                ,c(&Protocol::m_respBodyType) = daoPc.m_respBodyType
                ,c(&Protocol::m_reqBodyDataStatus) = (daoPc.m_reqBodyData.empty() ? 0 : 1)
                ,c(&Protocol::m_respBodyDataStatus) = (daoPc.m_respBodyData.empty() ? 0 : 1)
                ,c(&Protocol::m_reqCfg) = daoPc.m_reqCfg
                ,c(&Protocol::m_respCfg) = daoPc.m_respCfg
                ,c(&Protocol::m_reqBodyData) = std::move(daoPc.m_reqBodyData)
                ,c(&Protocol::m_respBodyData) = std::move(daoPc.m_respBodyData)
                ,c(&Protocol::m_isEndian) = std::move(daoPc.m_isEndian)
                ,c(&Protocol::m_utime) = now
            ), 
            where(
            c(&Protocol::m_id) == daoPc.m_id
            ));

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], type[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            daoPc.m_id,
            daoPc.m_type);
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateStatusById "<< "id= " << daoPc.m_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string &name)
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
        auto n = lease_result.val->db().count<kit_dao::Protocol>(where(
            c(&kit_dao::Protocol::m_id) == protocol_id
            &&
            c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return false;
        }

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }

        // UPDATE Protocols SET `status`= ?, `utime` = ? WHERE id = ?
        tx_result.val->db().update_all(
            set(
                c(&kit_dao::Protocol::m_name) = name
                ,c(&kit_dao::Protocol::m_utime) = now
            ),
            where(protocol_id == c(&kit_dao::Protocol::m_id))
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], name[%s] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            name.c_str());

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateName "<< "id= " << protocol_id << std::endl;

    return true;
}


bool SqliteOrmProtocolDao::UpdateReqCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& runtime_key, const nlohmann::json& cfg_json)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    std::string json_path;
    try {

        // SELECT COUNT(*) FROM `protocols` WHERE `id`= ? && `status` = ?;
        auto n = lease_result.val->db().count<kit_dao::Protocol>(where(
            c(&kit_dao::Protocol::m_id) == protocol_id
            &&
            c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return false;
        }

        /* 事务 */
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }

        // UPDATE protocols SET `runtime_key`= ?, `req_cfg` = ?, `utime` = ? WHERE id = ?
        tx_result.val->db().update_all(
            set(
                c(&kit_dao::Protocol::m_runtimeKey) = runtime_key
                ,c(&kit_dao::Protocol::m_reqCfg) = cfg_json.dump()
                // 更新修改时间
                ,c(&kit_dao::Protocol::m_utime) = now
            ),
            where(c(&kit_dao::Protocol::m_id) == protocol_id)
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], runtime_key[%s] cfg_json[%s] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            runtime_key.c_str(),
            cfg_json.dump().c_str());

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateReqCfg "<< "id= " << protocol_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateRespCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const nlohmann::json& cfg_json)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    std::string json_path;
    try {

        // SELECT COUNT(*) FROM `protocols` WHERE `id`= ?;
        auto n = lease_result.val->db().count<kit_dao::Protocol>(where(
            c(&kit_dao::Protocol::m_id) == protocol_id
            &&
            c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return false;
        }

        /* 事务 */
        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }

        // UPDATE protocols SET `resp_cfg` = ?, `utime` = ? WHERE id = ?
        tx_result.val->db().update_all(
            set(
                c(&kit_dao::Protocol::m_respCfg) = cfg_json.dump()
                ,c(&kit_dao::Protocol::m_utime) = now
            ),
            where(c(&kit_dao::Protocol::m_id) == protocol_id)
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], cfg_json[%s] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            cfg_json.dump().c_str());

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateRespCfg "<< "id= " << protocol_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, int32_t body_type, const std::vector<char>& body_data)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    const auto body_type_ptr =  side == 1 ? &kit_dao::Protocol::m_reqBodyType : &kit_dao::Protocol::m_respBodyType;
    const auto body_data_ptr = side == 1 ? &kit_dao::Protocol::m_reqBodyData : &kit_dao::Protocol::m_respBodyData;
    const auto body_status_ptr = side == 1 ? &kit_dao::Protocol::m_reqBodyDataStatus : &kit_dao::Protocol::m_respBodyDataStatus;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // SELECT COUNT(*) FROM `protocols` WHERE `id`= ?;
        auto n = lease_result.val->db().count<kit_dao::Protocol>(where(
            c(&kit_dao::Protocol::m_id) == protocol_id
            &&
            c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        ));
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
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
                c(body_type_ptr) = body_type
                ,c(body_data_ptr) = body_data
                ,c(body_status_ptr) = (body_data.size() ? 1 : 0)
                ,c(&kit_dao::Protocol::m_utime) = now
            ),
            where(c(&kit_dao::Protocol::m_id) == protocol_id)
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], side[%d], body_type[%d], body_size[%ld]\n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            side,
            body_type,
            body_data.size());
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateBody "<< "id= " << protocol_id << std::endl;

    return true;
}

kit_dao::Protocol SqliteOrmProtocolDao::GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    kit_dao::Protocol pc;
    pc.m_id = -1;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pc;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE id == protocol_id
        auto pc_ptr = lease_result.val->db().get_pointer<kit_dao::Protocol>(protocol_id);
        if(!pc_ptr)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return pc;
        }

        pc = std::move(*pc_ptr);

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id);
        return pc;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetById "<< pc.m_id << ", " << protocol_id << ", " << pc.m_reqBodyData.size() << ", " << pc.m_respBodyData.size() << std::endl;

    return pc;
}

std::vector<kit_dao::Protocol> SqliteOrmProtocolDao::ListByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status, int32_t offset, int32_t limit)
{
    std::vector<kit_dao::Protocol> pcs;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pcs;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列, 这里不查询Body数据

        // SELCT id,name,... FROM xxx WHERE m_userId = ? and status = ?
        // BUG: 多结构体映射同一个表 sqlite3不支持
#if 0
        auto tmps = lease_result.val->db().get_all<kit_dao::Protocol>(
            where(
                c(&ProtocolList::m_projectId) == project_id
                &&
                c(&ProtocolList::m_status) == status
            )
            ,order_by(&kit_dao::Protocol::m_ctime).desc()
            ,sqlite_orm::limit(offset, limit)
        );

        if(tmps.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pjId[%ld] \n", project_id);
            return pcs;
        }
        pcs.swap(tmps);


#else

        // 旧写法是利用元组vector<tutle<10>>
        auto tmps = lease_result.val->db().select(
        columns(
            &Protocol::m_id,
            &Protocol::m_name,
            &Protocol::m_type,
            &Protocol::m_projectId,
            &Protocol::m_status,
            &Protocol::m_configState,
            &Protocol::m_reqBodyType,
            &Protocol::m_respBodyType,
            &Protocol::m_reqBodyDataStatus,
            &Protocol::m_respBodyDataStatus,
            &Protocol::m_reqCfg,
            &Protocol::m_respCfg,
            &Protocol::m_isEndian,
            &Protocol::m_ctime,
            &Protocol::m_utime
        ),
        where(
            c(&Protocol::m_projectId) ==  project_id
            &&
            c(&Protocol::m_status) == status
        ),
        order_by(&Protocol::m_ctime).desc(),
        sqlite_orm::limit(offset, limit)
        );

        if(tmps.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pjId[%ld] \n", project_id);
            return pcs;
        }


        // 一边转换一边copy
        std::transform(tmps.begin(), tmps.end(), std::back_inserter(pcs), [](auto &item){
            // 全部移动 不要拷贝 查询量上去后很损耗性能
            Protocol p;
            p.m_id = std::move(std::get<0>(item));
            p.m_name = std::move(std::get<1>(item));
            p.m_type = std::move(std::get<2>(item));
            p.m_projectId = std::move(std::get<3>(item));
            p.m_status = std::move(std::get<4>(item));
            p.m_configState = std::move(std::get<5>(item));
            p.m_reqBodyType = std::move(std::get<6>(item));
            p.m_respBodyType = std::move(std::get<7>(item));

            p.m_reqBodyDataStatus= std::move(std::get<8>(item));
            p.m_respBodyDataStatus = std::move(std::get<9>(item));


            p.m_reqCfg = std::move(std::get<10>(item));
            p.m_respCfg = std::move(std::get<11>(item));
            p.m_isEndian = std::move(std::get<12>(item));
            p.m_ctime = std::move(std::get<13>(item));
            p.m_utime = std::move(std::get<14>(item));

            return p;
        });
#endif

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], status[%d], offset[%d], limit[%d]\n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            status,
            offset,
            limit);
        return pcs;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetByProject " << project_id << ", status= " << status << ", size= " << pcs.size() << std::endl;

    return pcs;
}

std::vector<kit_dao::Protocol> SqliteOrmProtocolDao::GetAll(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status, int32_t config_state)
{
    std::vector<kit_dao::Protocol> pcs;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return pcs;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列, 这里不查询Body数据

        // SELCT id,name,... FROM xxx WHERE project_id = ? and status = ?
        if(-1 == config_state)
        {
            pcs = lease_result.val->db().get_all<kit_dao::Protocol>(
                where(
                    c(&Protocol::m_projectId) == project_id
                    &&
                    c(&Protocol::m_status) == status
                )
            );
        }
        else
        {
            pcs = lease_result.val->db().get_all<kit_dao::Protocol>(
                where(
                    c(&Protocol::m_projectId) == project_id
                    &&
                    c(&Protocol::m_status) == status
                    &&
                    c(&Protocol::m_configState) == config_state
                )
            );
        }
        if(pcs.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pjId[%ld] \n", project_id);
            return pcs;
        }

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], status[%d]\n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            status);
        return pcs;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetByProject " << project_id << ", status= " << status << ", config_state= " << config_state << ", size= " << pcs.size() << std::endl;

    return pcs;
}


int32_t SqliteOrmProtocolDao::CountByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status)
{
    int32_t protocol_cnt = -1;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {

        // SELCT COUNT(*) FROM xxx WHERE `projec_id` = ? and `status` = ?

        protocol_cnt = lease_result.val->db().count<kit_dao::Protocol>(where(
            c(&kit_dao::Protocol::m_projectId) == project_id
            &&
            c(&kit_dao::Protocol::m_status) == status
        ));

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pjId[%ld], status[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            project_id,
            status);
        protocol_cnt = -1;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::CountByProject " << project_id << ", " << protocol_cnt << std::endl;

    return protocol_cnt;
}


// 注意: 这个接口弃用
#if 1
std::string SqliteOrmProtocolDao::GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side)
{
    auto cfg = side == 1 ? &kit_dao::Protocol::m_reqCfg : &kit_dao::Protocol::m_respCfg;

    std::vector<std::string> reses;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return "";
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id


        reses = lease_result.val->db().select(
            json_extract<std::string>(cfg, "$.common_fields"),
            where(c(&kit_dao::Protocol::m_id) == protocol_id)
        );



    } catch(const std::exception &e) {

        DAOPC_ERROR() << "GetById faild! " << e.what() << std::endl;

    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetTcpCommonFieldsById "<< protocol_id  << std::endl;

    return reses.at(0);
}
#endif

int32_t SqliteOrmProtocolDao::GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side)
{
    int32_t body_type = -1;
    const auto body_type_ptr = side == 1 ? &kit_dao::Protocol::m_reqBodyType : &kit_dao::Protocol::m_respBodyType;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    std::vector<int32_t> body_types;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id

        const auto& tmps = lease_result.val->db().select(body_type_ptr,
            where(
                c(&kit_dao::Protocol::m_id) == protocol_id)
                && c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        );

        if(tmps.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return -1;
        }
        if(tmps.size() > 1)
        {
            DAOPC_F_WARN("protocol not unique! pcId[%ld]: %ld \n", protocol_id, tmps.size());
        }

        body_type = tmps.at(0);

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], side[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            side);
        return -1;
    }


    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetBodyTypeById "<< protocol_id << ", " << body_type << std::endl;

    return body_type;
}

bool SqliteOrmProtocolDao::GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, std::vector<char> &body_data)
{
    const auto body_data_ptr = side == 1 ? &kit_dao::Protocol::m_reqBodyData : &kit_dao::Protocol::m_respBodyData;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id

        auto tmps = lease_result.val->db().select(body_data_ptr,
            where(
                c(&kit_dao::Protocol::m_id) == protocol_id
                && c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
        ));
        if(tmps.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return -1;
        }
        if(tmps.size() > 1)
        {
            DAOPC_F_WARN("protocol not unique! pcId[%ld]: %ld \n", protocol_id, tmps.size());
        }

        body_data.swap(tmps.at(0));

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], side[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            side);
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetBodyDataById "<< protocol_id << ", " << body_data.size() << std::endl;

    return true;
}


bool SqliteOrmProtocolDao::GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, int32_t &body_type, std::vector<char> &body_data)
{
    const auto body_type_ptr = side == 1 ? &kit_dao::Protocol::m_reqBodyType : &kit_dao::Protocol::m_respBodyType;
    const auto body_data_ptr = side == 1 ? &kit_dao::Protocol::m_reqBodyData : &kit_dao::Protocol::m_respBodyData;

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id

        auto tmps = lease_result.val->db().select(
            columns(
                body_type_ptr,
                body_data_ptr
            ),
            where(
                c(&kit_dao::Protocol::m_id) == protocol_id
                && c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
            )
        );
        if(tmps.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return false;
        }
        if(tmps.size() > 1)
        {
            DAOPC_F_WARN("protocol not unique! pcId[%ld]: %ld \n", protocol_id, tmps.size());
        }

        body_type = std::get<0>(tmps.at(0));
        body_data = std::move(std::get<1>(tmps.at(0)));

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld], side[%d] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            side);

        return false;
    }


    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetBodyInfoById "<< protocol_id << std::endl;

    return true;
}


nlohmann::json SqliteOrmProtocolDao::GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    nlohmann::json root = nlohmann::json::object();

    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return root;
    }
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_cfg, resp_cfg FROM xxx WHERE protocol_id

        auto tmps = lease_result.val->db().select(
            columns(
                &kit_dao::Protocol::m_reqCfg,
                &kit_dao::Protocol::m_respCfg
            ),
            where(
                c(&kit_dao::Protocol::m_id) == protocol_id
                && c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
            )
        );
        if(tmps.empty())
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return root;
        }
        if(tmps.size() > 1)
        {
            DAOPC_F_WARN("protocol not unique! pcId[%ld]: %ld \n", protocol_id, tmps.size());
        }

        root["req_cfg"] = nlohmann::json::parse(std::get<0>(tmps.at(0)));
        root["resp_cfg"] = nlohmann::json::parse(std::get<1>(tmps.at(0)));


    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id);

        return nlohmann::json::object();
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetCfgById "<< protocol_id << ", " << root.dump() << std::endl;

    return root;
}

std::optional<kit_dao::ProtocolAccessInfo> SqliteOrmProtocolDao::AccessProtocolAndProjectByJoin(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    ProtocolAccessInfo access_info;
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return std::nullopt;
    }
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        /*
            SELECT ...
            FROM `protocols` as pc
            INNER_JOIN `projects` as pj
            ON pj.id = pc.project_id
            WHERE pc.id = ?
        */

        auto tmps = lease_result.val->db().select(
            columns(
                &Protocol::m_id,
                &Protocol::m_projectId,
                &Protocol::m_runtimeKey,
                &Protocol::m_type,
                &Protocol::m_status,
                &Protocol::m_configState,
                &Project::m_userId,
                &Project::m_runtimeState,
                &Project::m_status
            )
            ,from<Protocol>()
            ,inner_join<Project>(
                on(c(&Protocol::m_projectId) == &Project::m_id)
            )
            ,where(
                c(&Protocol::m_id) == protocol_id
            )
        );
        if(tmps.empty())
        {
            DAOPC_F_ERROR("protocol dont exist! pcId[%ld] \n", protocol_id);
            return std::nullopt;
        }
        if(tmps.size() > 1)
        {
            DAOPC_F_WARN("protocol not unique! pcId[%ld] \n", protocol_id);
        }

        const auto& t = tmps.at(0);
        access_info.protocol_id = std::get<0>(t);
        access_info.project_id = std::get<1>(t);
        access_info.runtime_key = std::get<2>(t);
        access_info.protocol_type = std::get<3>(t);
        access_info.protocol_status = std::get<4>(t);
        access_info.protocol_config_state = std::get<5>(t);
        access_info.project_user_id = std::get<6>(t);
        access_info.project_runtime_state = std::get<7>(t);
        access_info.project_status = std::get<8>(t);

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld] \n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id);

        return std::nullopt;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::AccessProctolAndProjectByJoin "<< protocol_id << std::endl;

    return access_info;
}


bool SqliteOrmProtocolDao::UpdateConfigState(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t config_state)
{
    auto now = kit_muduo::TimeStamp::NowMs();
    auto lease_result = _db_pool->acquire();
    if(!lease_result.ok())
    {
        DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
        return false;
    }
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_cfg, resp_cfg FROM xxx WHERE protocol_id

        auto n = lease_result.val->db().count<kit_dao::Protocol>(
            where(
                c(&kit_dao::Protocol::m_id) == protocol_id
                && c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(kit_domain::ProtocolStatus::kValid)
            )
        );
        if(0 == n)
        {
            DAOPC_F_WARN("protocol dont exist! pcId[%ld] \n", protocol_id);
            return false;
        }

        auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
        if(!tx_result.ok())
        {
            return false;
        }


        // UPDATE Protocols SET `config_state`= ?, `utime` = ? WHERE id = ?
        tx_result.val->db().update_all(
            set(
                c(&kit_dao::Protocol::m_configState) = config_state
                ,c(&kit_dao::Protocol::m_utime) = now
            )
            ,where(c(&kit_dao::Protocol::m_id) == protocol_id)
        );

        tx_result.val->commit();

    } catch (const std::system_error &e) {

        DAOPC_F_ERROR(
            "%s pcId[%ld] config_state[%d]\n",
            MakeSqliteErrorMsg(e).c_str(),
            protocol_id,
            config_state);

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateConfigState "<< protocol_id << ", config_state: " << config_state << std::endl;

    return true;
}

} // namespace kit_domain
