/**
 * @file project_server.h
 * @brief 测试服务实际服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 14:35:41
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_PROJECT_SERVER_H__
#define __KIT_DOMAIN_PROJECT_SERVER_H__

#include "domain/runtime_result.h"
#include "net/call_backs.h"
#include "work/domain/type.h"
#include "net/inet_address.h"
#include "nlohmann/json.hpp"
#include "protocol_interaction.h"
#include "protocol_interaction_observation.h"
#include "net/tcp_server.h"

#include <memory>
#include <vector>

using nljson = nlohmann::json;


namespace kit_domain {

class Protocol;
class ProtocolItem;
class CustomTcpProtocolItem;
class HttpProtocolItem;
enum class ProtocolBodyType;
class CustomTcpPattern;
struct HttpItemReqHeaderCfg;
class CustomTcpMessage;
struct CustomTcpItemCfg;
class RuntimeLease;
class InteractionRecordCache;

class ProjectServer: public std::enable_shared_from_this<ProjectServer>
{
public:
    using ObserveCallback = std::function<void(ProtocolInteractionObservation)>;

    ProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop, const kit_muduo::InetAddress &addr, const std::string &name = "");

    virtual ~ProjectServer() = default;

    void setProjectId(int64_t project_id) { project_id_ = project_id;}
    int64_t getProjectId() const { return project_id_; }

    bool isActive() const;

    kit_muduo::EventLoop *getLoop();

    void setObserveCallback(ObserveCallback cb) { observe_cb_ = std::move(cb); }

    std::shared_ptr<InteractionRecordCache> cache() const { return notice_cache_; }

    virtual void start();
    virtual bool stop();

    virtual const kit_muduo::InetAddress& getBindAddr() const = 0;

    virtual RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) = 0;
    virtual RuntimeResult<void> DelProtocolItem(int64_t protocol_id) = 0;

    virtual RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) = 0;

    virtual RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson &req_cfg_json) = 0;

    virtual RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson &resp_cfg_json) = 0;

    virtual RuntimeResult<void> UpdateBodyProtocolItem(int64_t protocol_id, ProtocolSide side, const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;


    virtual RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;

    virtual RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;

    virtual std::shared_ptr<CustomTcpPattern> GetPatternInfo() = 0;

protected:
    virtual void closeAllProtocolInteractionCaches() = 0;

protected:

    void emitObserve(ProtocolInteractionObservation obs);

protected:
    kit_muduo::TcpServer tcp_server_;
    /// @brief 测试服务id
    int64_t project_id_;
    /// @brief 租赁Loop
    std::shared_ptr<RuntimeLease> lease_loop_;
    /// @brief 当前运行态
    std::atomic_bool stopped_{false};
    /// @brief 观测回调
    ObserveCallback observe_cb_;
    /// @brief 协议项交互实时流缓存
    std::shared_ptr<InteractionRecordCache> notice_cache_;
};

bool WaitRuntimeStopDone(const char *name,
    int64_t project_id,
    const std::function<void(std::function<void()>)> &start_stop);

}
#endif
