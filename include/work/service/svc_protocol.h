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

#include <vector>

namespace kit_domain {

class Protocol;
class ProtocolRepoInterface;
enum class ProtocolBodyType;

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

    virtual bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const std::string& cfg_data) = 0;

    virtual bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json) = 0;

    virtual bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t body_type, const std::vector<char>& cfg_data) = 0;


    virtual Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;

    virtual std::vector<Protocol> GetByProject(kit_muduo::HttpContextPtr ctx, int64_t userId, int32_t offset, int32_t limit) = 0;

    virtual std::vector<Protocol> GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;

    virtual int32_t GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;


    virtual nlohmann::json GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) = 0;

    virtual ProtocolBodyType GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) = 0;

    virtual bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data) = 0;

    virtual bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, ProtocolBodyType &body_type, std::vector<char> &body_data) = 0;

    virtual nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) = 0;


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

    bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const std::string& cfg_data) override;

    bool UpdateCfg(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, const nlohmann::json& cfg_json) override;

    bool UpdateBody(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, int32_t body_type, const std::vector<char>& cfg_data) override;

    Protocol GetById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;
    
    std::vector<Protocol> GetByProject(kit_muduo::HttpContextPtr ctx, int64_t projectId, int32_t offset, int32_t limit) override;

    std::vector<Protocol> GetActiveByProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;

    int32_t GetProtocolCnt(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;

    nlohmann::json GetTcpCommonFieldsById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) override;

    ProtocolBodyType GetBodyTypeById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp) override;

    bool GetBodyDataById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, std::vector<char> &body_data) override;

    bool GetBodyInfoById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id, int32_t req_or_resp, ProtocolBodyType &body_type, std::vector<char> &body_data) override;

    nlohmann::json GetCfgById(kit_muduo::HttpContextPtr ctx, int64_t protocol_id) override;
};


} // namespace kit_domain
#endif // __KIT_SVC_PROJECT_H__