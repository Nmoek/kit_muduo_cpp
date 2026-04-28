/**
 * @file dao_protocol.cpp
 * @brief 协议项 dao层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-29 16:40:22
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "dao/dao_protocol.h"
#include "domain/protocol.h"
#include "dao/dao_log.h"
#include "base/time_stamp.h"

#include <thread>

using nljson = nlohmann::json;
 
namespace kit_domain {

int64_t SqliteOrmProtocolDao::Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Protocol daoPc)
{
    daoPc.m_id = 0;
    auto now = kit_muduo::TimeStamp::NowMs();
    daoPc.m_ctime = daoPc.m_utime = now;

    int retry = 3;
    int64_t protocol_id = -1;
    while(retry--)
    {
        try {
            // 这个锁保护的是事务开启动作 + 写入动作
            std::lock_guard<std::mutex> lock(_writeMtx);
            // 系统设计问题 SQLite不支持高并发写 WAL模式仅支持 TPS:100 ~ 300 否则迁移Mysql/PostorgeSql
            _db->begin_transaction();
            protocol_id = _db->insert(daoPc);
            _db->commit();

            break;
        } catch (const std::system_error& code) {
            _db->rollback();

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
            _db->rollback();
            protocol_id = -1;
            throw;
        }

    }

    DAODB_INFO() << "qliteOrmProtocolDao::Insert, id= " << protocol_id << std::endl;

    return protocol_id;
}

bool SqliteOrmProtocolDao::UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t status)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE id = ? 
        auto pc = _db->get_pointer<kit_dao::Protocol>(protocol_id);
        if(!pc)
        {
            DAOPC_F_WARN("protocol dont exist! id=%ld \n", protocol_id);
            return false;
        }

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();
        pc->m_status = status;
        pc->m_utime = now;
        _db->update(*pc);
        _db->commit();

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "UpdateStatusById faild! " << "id= " << protocol_id << ", " << e.what() << std::endl;
        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateStatusById "<< "id= " << protocol_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateById(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) 
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE id = ? 
        auto pc = _db->get_pointer<kit_dao::Protocol>(daoPc.m_id);
        if(!pc)
        {
            DAOPC_F_WARN("protocol dont exist! id=%ld \n", daoPc.m_id);
            return false;
        }
        pc->m_name = daoPc.m_name;
        pc->m_reqBodyType = daoPc.m_reqBodyType;
        pc->m_respBodyType = daoPc.m_respBodyType;
        pc->m_reqCfg = std::move(daoPc.m_reqCfg);
        pc->m_respCfg = std::move(daoPc.m_respCfg);
        pc->m_reqBodyData = std::move(daoPc.m_reqBodyData);
        pc->m_respBodyData = std::move(daoPc.m_respBodyData);
        pc->m_utime = now;

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();
        _db->update(*pc);
        _db->commit();

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "UpdateStatusById faild! " << "id= " << daoPc.m_id << ", " << e.what() << std::endl;
        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateStatusById "<< "id= " << daoPc.m_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string &name)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    try {

        auto pc = _db->get_pointer<kit_dao::Protocol>(protocol_id);
        if(!pc)
        {
            DAOPC_F_WARN("protocol dont exist! id=%ld \n", protocol_id);
            return false;
        }

        pc->m_name = name;
        pc->m_utime = now;

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();
        _db->update(*pc);
        _db->commit();

    } catch(const std::exception& e) {
   
        DAOPC_ERROR() << "UpdateName faild! " << "id= " << protocol_id << ", " << e.what() << std::endl;
        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateName "<< "id= " << protocol_id << std::endl;

    return true;
}


// 这个接口是整个配置进行替换
bool SqliteOrmProtocolDao::UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const std::string& cfg_data)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    const auto field_ptr = req_or_resp == 1 ? &kit_dao::Protocol::m_reqCfg : &kit_dao::Protocol::m_respCfg;

    // 1，req_cfg 整个json更新 (目前先采用这种)
    // 2. req_cfg 利用json路径更新
    try {

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        /* 事务 */
        _db->begin_immediate_transaction();

        _db->update_all(
            sqlite_orm::set(
                // 更新json
                sqlite_orm::c(field_ptr) = cfg_data,
                // 更新修改时间
                sqlite_orm::c(&kit_dao::Protocol::m_utime) = now
            ),
            sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
        );


        _db->commit();
        /* 事务 */

    } catch(const std::exception& e) {
   
        DAOPC_ERROR() << "UpdateCfg faild! " << "id= " << protocol_id << ", " << e.what() << std::endl;

        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateCfg "<< "id= " << protocol_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json)
{
    // 默认以第一个对象更新
    const std::string& json_path = "$." + cfg_json.begin().key();
    const auto& json_value = cfg_json.begin().value();
    auto field_ptr = req_or_resp == 1 ? &kit_dao::Protocol::m_reqCfg : &kit_dao::Protocol::m_respCfg;

    DAOPC_F_DEBUG("UpdateCfg id[%d], json_path[%s], json_type[%s], json_value[%s] \n", protocol_id, json_path.c_str(), cfg_json.begin().value().type_name(), json_value.dump().c_str());

    auto now = kit_muduo::TimeStamp::Now().millSeconds();

    try {
        std::lock_guard<std::mutex> lock(_writeMtx);
        /* 事务 */
        _db->begin_immediate_transaction();

        // UPDATE protocols SET `req_cfg`= JSON_REPALCE(`req_cfg`, ?, ?) WHERE id = ? 
        _db->update_all(
            sqlite_orm::set(
                sqlite_orm::c(field_ptr) = sqlite_orm::json_replace(field_ptr, json_path, sqlite_orm::json(json_value.dump())),
                // 更新修改时间
                sqlite_orm::c(&kit_dao::Protocol::m_utime) = now
            ),
            sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
        );
        _db->commit();

    } catch (const std::exception& e) {

        DAOPC_F_ERROR("SqliteOrmProtocolDao::UpdateCfg json字段更新失败! %s. %d, %s, %s, \n",e.what(), protocol_id, typeid(field_ptr).name(), json_path.c_str());
        _db->rollback();

        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateCfg "<< "id= " << protocol_id << std::endl;

    return true;
}

bool SqliteOrmProtocolDao::UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t body_type, const std::vector<char>& body_data)
{
    auto now = kit_muduo::TimeStamp::Now().millSeconds();
    const auto body_data_ptr = req_or_resp == 1 ? &kit_dao::Protocol::m_reqBodyData : &kit_dao::Protocol::m_respBodyData;
    const auto body_status_ptr = req_or_resp == 1 ? &kit_dao::Protocol::m_reqBodyDataStatus : &kit_dao::Protocol::m_respBodyDataStatus;

    try {
#if 0
        auto pc = _db->get_pointer<kit_dao::Protocol>(protocol_id);
        if(!pc)
        {
            DAOPC_F_WARN("protocol dont exist! id=%ld \n", protocol_id);
            return false;
        }

        if(1 == req_or_resp)
        {
            pc->m_reqBodyData = std::move(cfg_data);
            pc->m_reqBodyType = body_type;
            pc->m_reqBodyDataStatus = cfg_data.empty() ? 0 : 1;
        }
        else if(2 == req_or_resp)
        {
            pc->m_respBodyData = std::move(cfg_data);
            pc->m_respBodyType = body_type;
            pc->m_respBodyDataStatus = cfg_data.empty() ? 0 : 1;

        }
        else
            return false;

        
        pc->m_utime = now;
#endif

        // UPDATE Protocols SET `status`= ? WHERE id = ? 
        std::lock_guard<std::mutex> lock(_writeMtx);
        _db->begin_immediate_transaction();

        _db->update_all(
            sqlite_orm::set(
                // 更新json
                sqlite_orm::c(body_data_ptr) = body_data,
                sqlite_orm::c(body_status_ptr) = body_data.size() ? 1 : 0,
                // 更新修改时间
                sqlite_orm::c(&kit_dao::Protocol::m_utime) = now
            ),
            sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
        );


        _db->commit();

    } catch(const std::exception& e) {
   
        DAOPC_ERROR() << "UpdateBody faild! " << "id= " << protocol_id << ", " << e.what() << std::endl;
        _db->rollback();
        return false;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::UpdateBody "<< "id= " << protocol_id << std::endl;

    return true;
}

kit_dao::Protocol SqliteOrmProtocolDao::GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    kit_dao::Protocol pc;
    pc.m_id = -1;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE id == protocol_id 
        auto pc_ptr = _db->get_pointer<kit_dao::Protocol>(protocol_id);
        if(!pc_ptr)
        {
            DAOPJ_INFO() << "GetById protocol not exist! id= " << protocol_id << std::endl;
            return pc;
        }

        pc = std::move(*pc_ptr);

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetById faild! " << e.what() << std::endl;
        return pc;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetById "<< pc.m_id << ", " << protocol_id << ", " << pc.m_reqBodyData.size() << ", " << pc.m_respBodyData.size() << std::endl;

    return pc;
}

std::vector<kit_dao::Protocol> SqliteOrmProtocolDao::GetByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t offset, int32_t limit)
{
    std::vector<kit_dao::Protocol> pcs;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT id,name,... FROM xxx WHERE m_userId = ? and status = ?
        // tmps 是个元组vector<tutle<10>>
        auto tmps = _db->select(sqlite_orm::columns(
            &kit_dao::Protocol::m_id,
            &kit_dao::Protocol::m_name,
            &kit_dao::Protocol::m_type,
            &kit_dao::Protocol::m_projectId,
            &kit_dao::Protocol::m_reqBodyType,
            &kit_dao::Protocol::m_respBodyType,
            &kit_dao::Protocol::m_reqBodyDataStatus,
            &kit_dao::Protocol::m_respBodyDataStatus,
            &kit_dao::Protocol::m_reqCfg,
            &kit_dao::Protocol::m_respCfg,
            &kit_dao::Protocol::m_ctime,
            &kit_dao::Protocol::m_utime),
        sqlite_orm::where(
            sqlite_orm::c(&kit_dao::Protocol::m_projectId) == project_id
            and
            sqlite_orm::c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(ProtocolStatus::ACTIVE)
        ), 
        sqlite_orm::order_by(&kit_dao::Protocol::m_ctime).desc(),
        sqlite_orm::limit(offset, limit)
        );

        // 一边转换一边copy
        std::transform(tmps.begin(), tmps.end(), std::back_inserter(pcs), [](auto &item){
            // 全部移动 不要拷贝 查询量上去后很损耗性能
            kit_dao::Protocol p;
            p.m_id = std::move(std::get<0>(item));
            p.m_name = std::move(std::get<1>(item));
            p.m_type = std::move(std::get<2>(item));
            p.m_projectId = std::move(std::get<3>(item));
            p.m_reqBodyType = std::move(std::get<4>(item));
            p.m_respBodyType = std::move(std::get<5>(item));

            p.m_reqBodyDataStatus= std::move(std::get<6>(item));
            p.m_respBodyDataStatus = std::move(std::get<7>(item));


            p.m_reqCfg = std::move(std::get<8>(item));
            p.m_respCfg = std::move(std::get<9>(item));
            p.m_ctime = std::move(std::get<10>(item));
            p.m_utime = std::move(std::get<11>(item));

            return p;
        });

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetByProject faild! " << e.what() << std::endl;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetByProject " << project_id << ", " << offset << ", " << limit << ", size= " << pcs.size() << std::endl;

    return pcs;
}



std::vector<kit_dao::Protocol> SqliteOrmProtocolDao::GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    std::vector<kit_dao::Protocol> pcs;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT * FROM xxx WHERE m_projectId = ? and status = ?
        pcs = _db->get_all<kit_dao::Protocol>(
        sqlite_orm::where(
            sqlite_orm::c(&kit_dao::Protocol::m_projectId) == project_id &&
            sqlite_orm::c(&kit_dao::Protocol::m_status) == static_cast<int32_t>(ProtocolStatus::ACTIVE))
        );

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetActiveByProject faild! " << e.what() << std::endl;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetActiveByProject " << project_id << ", size= " << pcs.size() << std::endl;

    return pcs;
}



int32_t SqliteOrmProtocolDao::CountByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    int32_t protocol_cnt = -1;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT id,name,... FROM xxx WHERE m_userId = ? and status = ?
        // tmps 是个元组vector<tutle<10>>

        protocol_cnt = _db->count<kit_dao::Protocol>(sqlite_orm::where(
            sqlite_orm::c(&kit_dao::Protocol::m_projectId) == project_id
            && sqlite_orm::c(&kit_dao::Protocol::m_status) == 1
        ));

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "CountByProject faild! " << e.what() << std::endl;
        protocol_cnt = -1;
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::CountByProject " << project_id << ", " << protocol_cnt << std::endl;

    return protocol_cnt;
}

std::string SqliteOrmProtocolDao::GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp)
{
    auto cfg = req_or_resp == 1 ? &kit_dao::Protocol::m_reqCfg : &kit_dao::Protocol::m_respCfg;

    std::vector<std::string> reses;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id 


        reses = _db->select(
            sqlite_orm::json_extract<std::string>(cfg, "$.common_fields"),
            sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
        );
        


    } catch(const std::exception &e) {

        DAOPC_ERROR() << "GetById faild! " << e.what() << std::endl;
        
    }
    
    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetTcpCommonFieldsById "<< protocol_id  << std::endl;

    return reses.at(0);
}


int32_t SqliteOrmProtocolDao::GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp)
{
    int32_t body_type = -1;
    std::vector<int32_t> body_types;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id 
        if(1 == req_or_resp)
        {
            body_types = _db->select(&kit_dao::Protocol::m_reqBodyType,
                sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
            );
        }
        else if(2 == req_or_resp)
        {
            body_types = _db->select(&kit_dao::Protocol::m_respBodyType,
                sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
            );
        }

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetBodyTypeById faild! " << e.what() << std::endl;
        body_type = -1;
    }
    
    body_type = body_types.empty() ? -1 : body_types[0];
    
    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetBodyTypeById "<< protocol_id << ", " << body_type << std::endl;

    return body_type;
}

bool SqliteOrmProtocolDao::GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data)
{
    std::vector<std::vector<char>> tmp_datas;
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id 
        if(1 == req_or_resp)
        {
            tmp_datas = _db->select(&kit_dao::Protocol::m_reqBodyData,
                sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
            );
        }
        else if(2 == req_or_resp)
        {
            tmp_datas = _db->select(&kit_dao::Protocol::m_respBodyData,
                sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
            );
        }

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetBodyDataById faild! " << e.what() << std::endl;
        return false;
    }

    body_data = std::move(tmp_datas[0]);

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetBodyDataById "<< protocol_id << ", " << body_data.size() << std::endl;

    return true;
}


bool SqliteOrmProtocolDao::GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t &body_type, std::vector<char> &body_data)
{

    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_body_type FROM xxx WHERE protocol_id 
        if(1 == req_or_resp)
        {
            auto tmp_datas = _db->select(
                sqlite_orm::columns(
                    &kit_dao::Protocol::m_reqBodyType,
                    &kit_dao::Protocol::m_reqBodyData
                ),
                sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
            );
            
            for(auto &t : tmp_datas)
            {
                body_type = std::get<0>(t);
                body_data = std::move(std::get<1>(t));
            }
        }
        else if(2 == req_or_resp)
        {
            auto tmp_datas = _db->select(
                sqlite_orm::columns(
                    &kit_dao::Protocol::m_respBodyType,
                    &kit_dao::Protocol::m_respBodyData
                ),
                sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
            );

            for(auto &t : tmp_datas)
            {
                body_type = std::get<0>(t);
                body_data = std::move(std::get<1>(t));
            }
        }

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetBodyInfoById faild! " << e.what() << std::endl;
        return false;
    }


    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetBodyInfoById "<< protocol_id << std::endl;

    return true;
}


nlohmann::json SqliteOrmProtocolDao::GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)
{
    nlohmann::json root = nlohmann::json::object();
    try {
        // 注意 查询指令顺序需要自己排列，orm框架不会自动排列
        // SELCT req_cfg, resp_cfg FROM xxx WHERE protocol_id 

        auto tmp_datas = _db->select(
            sqlite_orm::columns(
                &kit_dao::Protocol::m_reqCfg,
                &kit_dao::Protocol::m_respCfg
            ),
            sqlite_orm::where(sqlite_orm::c(&kit_dao::Protocol::m_id) == protocol_id)
        );

        for(auto &t : tmp_datas)
        {
            root["req_cfg"] = nlohmann::json::parse(std::get<0>(t));
            root["resp_cfg"] = nlohmann::json::parse(std::get<1>(t));
        }

    } catch(const std::exception &e) {
        DAOPC_ERROR() << "GetById faild! " << e.what() << std::endl;

        return nlohmann::json::object();
    }

    DAOPC_DEBUG() << "SqliteOrmProtocolDao::GetCfgById "<< protocol_id << ", " << root.dump() << std::endl;

    return root;
}

} // namespace kit_domain
