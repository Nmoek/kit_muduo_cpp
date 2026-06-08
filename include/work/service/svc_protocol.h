/**
 * @file svc_protocol.h
 * @brief 测试协议项 servie层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-25 18:15:08
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_SVC_PROTOCOL_H__
#define __KIT_SVC_PROTOCOL_H__

#include "net/call_backs.h"
#include "nlohmann/json.hpp"
#include "domain/type.h"

#include <vector>

namespace kit_domain {

struct Protocol;
struct ProtocolAccessInfo;
class ProtocolRepoInterface;

class ProtocolSvcInterface
{
public:
    ProtocolSvcInterface(std::shared_ptr<ProtocolRepoInterface> repo): _repo(repo) { }
    virtual ~ProtocolSvcInterface() = default;

    virtual int64_t Add(kit_muduo::HttpContextPtr ctx, Protocol &domainPc) = 0;

    virtual bool Del(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;

    virtual bool ReCover(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;

    virtual bool UpdateById(kit_muduo::HttpContextPtr ctx, Protocol &domainPc) = 0;

    virtual bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& name) = 0;

    virtual bool UpdateReqCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json) = 0;

    virtual bool UpdateRespCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json) = 0;

    virtual bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char>& cfg_data) = 0;


    virtual Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;

    virtual std::vector<Protocol> GetByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id, ProtocolStatus status, int32_t offset, int32_t limit) = 0;

    virtual std::vector<Protocol> GetValidByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual std::vector<Protocol> GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual int32_t GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolStatus status) = 0;


    virtual nlohmann::json GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side) = 0;

    virtual ProtocolBodyType GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side) = 0;

    virtual bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, std::vector<char> &body_data) = 0;

    virtual bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType &body_type, std::vector<char> &body_data) = 0;

    virtual nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;

    virtual bool GetAccessInfo(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolAccessInfo& access_info) = 0;

    virtual bool UpdateRuntimeEnabled(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolRuntimeEnabled runtime_enabled) = 0;


protected:
    bool validateProtocolCfg(ProtocolType type, ProtocolSide side, nlohmann::json cfg_json);

protected:
    std::shared_ptr<ProtocolRepoInterface> _repo;
};

class ProtocolService: public ProtocolSvcInterface
{
public:
    ProtocolService(std::shared_ptr<ProtocolRepoInterface> repo);

    ~ProtocolService();

    int64_t Add(kit_muduo::HttpContextPtr ctx, Protocol &domainPc) override;

    bool Del(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

    bool ReCover(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

    bool UpdateById(kit_muduo::HttpContextPtr ctx, Protocol &domainPc) override;

    bool UpdateName(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, const std::string& name) override;

    virtual bool UpdateReqCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json) override;

    virtual bool UpdateRespCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolType type, const nlohmann::json& cfg_json) override;

    bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char>& cfg_data) override;

    Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;
    
    std::vector<Protocol> GetByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, ProtocolStatus status, int32_t offset, int32_t limit) override;

    std::vector<Protocol> GetValidByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    std::vector<Protocol> GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    int32_t GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolStatus status) override;

    nlohmann::json GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side) override;

    ProtocolBodyType GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side) override;

    bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, std::vector<char> &body_data) override;

    bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolSide side, ProtocolBodyType &body_type, std::vector<char> &body_data) override;

    nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

    bool GetAccessInfo(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolAccessInfo& access_info) override;


    bool UpdateRuntimeEnabled(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, ProtocolRuntimeEnabled runtime_enabled) override;
};


} // namespace kit_domain
#endif // __KIT_SVC_PROJECT_H__