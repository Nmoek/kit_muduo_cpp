/**
 * @file dao_protocol.h
 * @brief 协议项 dao层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-29 16:24:03
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DAO_PROTOOL_H__
#define __KIT_DAO_PROTOOL_H__

#include "dao/protocol.h"
#include "net/call_backs.h"
#include "dao/init.h"
#include "nlohmann/json.hpp"
#include "base/time_stamp.h"
#include "dao/dao_log.h"
#include "dao/sqlite_orm_pool.h"

#include <memory>
#include <mutex>
#include <vector>

namespace kit_dao
{


class ProtocolDaoInterface
{
public:
    virtual ~ProtocolDaoInterface() = default;


    virtual int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) = 0;

    virtual bool UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t status) = 0;

    virtual bool UpdateById(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) = 0;

    virtual bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocolId, const std::string &name) = 0;

    virtual bool UpdateReqCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& runtime_key, const nlohmann::json& cfg_json) = 0;

    virtual bool UpdateRespCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const nlohmann::json& cfg_json) = 0;

    virtual bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t side, int32_t body_type, const std::vector<char>& cfg_data) = 0;

    virtual kit_dao::Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocolId) = 0;

    virtual std::vector<kit_dao::Protocol> ListByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, int32_t status, int32_t offset, int32_t limit) = 0;

    virtual std::vector<kit_dao::Protocol> GetAll(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status, int32_t config_state) = 0;


    virtual int32_t CountByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status) = 0;

    // 这个接口弃用
    virtual std::string GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side) = 0;

    virtual int32_t GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side) = 0;

    virtual bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, std::vector<char> &body_data) = 0;

    virtual bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, int32_t &body_type, std::vector<char> &body_data) = 0;

    virtual nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)  = 0;

    virtual std::optional<kit_dao::ProtocolAccessInfo> AccessProtocolAndProjectByJoin(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;

    virtual bool UpdateConfigState(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t config_state) = 0;

};

class SqliteOrmProtocolDao : public ProtocolDaoInterface
{
public:
    explicit SqliteOrmProtocolDao(std::shared_ptr<kit_dao::SqliteOrmPool> db_pool);
    ~SqliteOrmProtocolDao() = default;

    int64_t Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Protocol daoPc) override;

    bool UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t status) override;

    bool UpdateById(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) override;

    bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocolId, const std::string &name) override;

    bool UpdateReqCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& runtime_key, const nlohmann::json& cfg_json) override;

    bool UpdateRespCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const nlohmann::json& cfg_json) override;

    bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t side, int32_t body_type, const std::vector<char>& cfg_data) override;

    kit_dao::Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocolId) override;

    std::vector<kit_dao::Protocol> ListByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, int32_t status, int32_t offset, int32_t limit) override;

    std::vector<kit_dao::Protocol> GetAll(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status, int32_t config_state) override;

    int32_t CountByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, int32_t status) override;

    std::string GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side) override;

    int32_t GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side) override;

    bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, std::vector<char> &body_data) override;

    bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t side, int32_t &body_type, std::vector<char> &body_data) override;

    nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

    std::optional<kit_dao::ProtocolAccessInfo> AccessProtocolAndProjectByJoin(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

    bool UpdateConfigState(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t config_state) override;

private:
    template<typename T, typename Field, typename Value>
    bool UpdateJsonField(int64_t id, const std::string& json_path, Value&& new_value, Field T::* field_ptr)
    {
        auto now = kit_muduo::TimeStamp::Now().millSeconds();
        auto lease_result = _db_pool->acquire();
        if(!lease_result.ok())
        {
            DAOPC_F_ERROR("sqlite connection lease error: %d\n", lease_result.toInt());
            return false;
        }

        try {

            /* 事务 */
            auto tx_result = SqliteOrmWriteTransaction::Create(lease_result.val, 3000);
            if(!tx_result.ok())
            {
                DAOPC_F_ERROR("sqlite begin write transaction error: %d\n", tx_result.toInt());
                return false;
            }
            // UPDATE protocols SET `req_cfg`= JSON_REPALCE(`req_cfg`, ?, ?) WHERE id = ? 
            tx_result.val->db().update_all(
                sqlite_orm::set(
                    sqlite_orm::c(field_ptr) = sqlite_orm::json_replace(field_ptr, json_path, std::forward<Value>(new_value)),
                    // 更新修改时间
                    sqlite_orm::c(&kit_dao::Protocol::m_utime) = now
                ),
                sqlite_orm::where(sqlite_orm::c(&T::m_id) == id)
            );
            tx_result.val->commit();

        } catch (const std::exception& e) {

            DAOPC_F_ERROR("%d, %s, %s, json字段更新失败! %s\n", id, typeid(field_ptr).name(), json_path, e.what());

            return false;
        }
        return true;
    }

#if 0
    template<typename T, typename Field, typename Value>
    bool UpdateJsonField(int64_t id, const std::string& json_path, Value&& new_value, Field T::* field_ptr)
    {
        auto now = kit_muduo::TimeStamp::Now().millSeconds();

        try {
            // UPDATE Protocols SET `status`= ? WHERE id = ? 
            auto updateStatement = _db->prepare(update_all(
                sqlite_orm::set(
                    sqlite_orm::c(field_ptr) = sqlite_orm::json_replace(field_ptr, json_path, std::placeholders::_1),
                    // 更新修改时间
                    sqlite_orm::c(&kit_dao::Protocol::m_utime) = now
                ),
                sqlite_orm::where(sqlite_orm::c(&T::m_id) == id)
            ));
      
            DAOPC_INFO() << "updateStatement: " << updateStatement.sql() << std::endl;

            std::lock_guard<std::mutex> lock(_writeMtx);
            /* 事务 */
            _db->begin_immediate_transaction();

            _db->execute(updateStatement, std::forward<Value>(new_value));
            
            _db->commit();

        } catch (const std::exception& e) {

            DAOPC_F_ERROR("%d, %s, %s, json字段更新失败! %s\n", id, typeid(field_ptr).name(), json_path, e.what());
            _db->rollback();

            return false;
        }
        return true;
    }
#endif

private:
    std::shared_ptr<kit_dao::SqliteOrmPool> _db_pool;
};


} // namespace kit_domain
#endif // __KIT_DAO_PROJECT_H__