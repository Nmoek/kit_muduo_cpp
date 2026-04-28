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

#include "net/call_backs.h"
#include "dao/init.h"
#include "nlohmann/json.hpp"
#include "base/time_stamp.h"
#include "dao/dao_log.h"

#include <memory>
#include <mutex>
#include <vector>

namespace kit_domain
{


class ProtocolDaoInterface
{
public:
    virtual ~ProtocolDaoInterface() = default;

    virtual int64_t Insert(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) = 0;

    virtual bool UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t status) = 0;

    virtual bool UpdateById(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) = 0;

    virtual bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocolId, const std::string &name) = 0;

    virtual bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t req_or_resp, const std::string& cfg_data) = 0;


    virtual bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json) = 0;

    virtual bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t req_or_resp, int32_t body_type, const std::vector<char>& cfg_data) = 0;

    virtual kit_dao::Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocolId) = 0;

    virtual std::vector<kit_dao::Protocol> GetByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, int32_t offset, int32_t limit) = 0;

    virtual std::vector<kit_dao::Protocol> GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual int32_t CountByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;


    virtual std::string GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) = 0;

    virtual int32_t GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) = 0;

    virtual bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data) = 0;

    virtual bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t &body_type, std::vector<char> &body_data) = 0;

    virtual nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id)  = 0;

};

class SqliteOrmProtocolDao : public ProtocolDaoInterface
{
public:
    SqliteOrmProtocolDao(std::shared_ptr<kit_dao::SqliteOrmType> db)
        :_db(db)
    {

    }
    ~SqliteOrmProtocolDao() = default;

    int64_t Insert(std::shared_ptr<kit_muduo::http::HttpContext> ctx, kit_dao::Protocol daoPc) override;

    bool UpdateStatusById(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t status) override;

    bool UpdateById(kit_muduo::HttpContextPtr ctx, kit_dao::Protocol daoPc) override;

    bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocolId, const std::string &name) override;

    bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t req_or_resp, const std::string& cfg_data) override;

    bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json) override;

    bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocolId, int32_t req_or_resp, int32_t body_type, const std::vector<char>& cfg_data) override;

    kit_dao::Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocolId) override;

    std::vector<kit_dao::Protocol> GetByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, int32_t offset, int32_t limit) override;

    std::vector<kit_dao::Protocol> GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    int32_t CountByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    std::string GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) override;

    int32_t GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) override;

    bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data) override;

    bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t &body_type, std::vector<char> &body_data) override;

    nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

private:
    template<typename T, typename Field, typename Value>
    bool UpdateJsonField(int64_t id, const std::string& json_path, Value&& new_value, Field T::* field_ptr)
    {
        auto now = kit_muduo::TimeStamp::Now().millSeconds();

        try {
            std::lock_guard<std::mutex> lock(_writeMtx);
            /* 事务 */
            _db->begin_immediate_transaction();

            // UPDATE protocols SET `req_cfg`= JSON_REPALCE(`req_cfg`, ?, ?) WHERE id = ? 
            _db->update_all(
                sqlite_orm::set(
                    sqlite_orm::c(field_ptr) = sqlite_orm::json_replace(field_ptr, json_path, std::forward<Value>(new_value)),
                    // 更新修改时间
                    sqlite_orm::c(&kit_dao::Protocol::m_utime) = now
                ),
                sqlite_orm::where(sqlite_orm::c(&T::m_id) == id)
            );
            _db->commit();

        } catch (const std::exception& e) {

            DAOPC_F_ERROR("%d, %s, %s, json字段更新失败! %s\n", id, typeid(field_ptr).name(), json_path, e.what());
            _db->rollback();

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
    std::shared_ptr<kit_dao::SqliteOrmType> _db;
    std::mutex _writeMtx;
};


} // namespace kit_domain
#endif // __KIT_DAO_PROJECT_H__