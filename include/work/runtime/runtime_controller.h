/**
 * @file runtime_controller.h
 * @brief 运行态业务管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-08 15:20:11
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_RUNTIME_CONTROLLER_H__
#define __KIT_RUNTIME_CONTROLLER_H__
#include "base/noncopyable.h"
#include "domain/project.h"
#include "domain/runtime_loop_pool.h"
#include "domain/runtime_result.h"
#include "domain/type.h"
#include "net/call_backs.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>


namespace kit_domain {

class ProjectServer;
class ProjectSvcInterface;
class ProtocolSvcInterface;

struct Protocol;

/// @brief 运行态操作类型
enum class RuntimeOperationKind
{
    // project类操作
    kStartProject,
    kStopProject,
    kRecoverProject,
    kReloadProject,
    kDeleteProject,
    kEditPatternInfo,

    // protocol类操作
    kAddProtocol,
    kEnableProtocol,
    kDisableProtocol,
    kDeleteProtocol,
    kUpdateProtocolCfg,
    kUpdateProtocolBody,
    kReconfigProtocol,

};

/// @brief 运行态操作后状态码
enum class RuntimeControlCode
{
    kOk = 0,

    kInvalidArgument,
    kProjectNotFound,
    kProjectDeleted,
    kProjectTypeInvalid,
    kProtocolNotFound,
    kProtocolDeleted,
    kProtocolTypeInvalid,

    kLoopLeaseFailed,
    kCreateServerFailed,
    kHydrateProtocolFailed,
    kCreateProtocolItemFailed,
    kRuntimeKeyInvalid,
    kRuntimeApplyFailed,
    kRuntimeRollbackFailed,

    kPersistFailed,
    kServiceFailed,
    kInternalError,
};

/**
 * @brief 控制面命令状态：回答“本次命令整体为什么成功或失败”。
 */
struct RuntimeCommandStatus
{
    RuntimeControlCode code{RuntimeControlCode::kOk};
    std::string message;
    RuntimeError runtime_err{RuntimeError::kOk};

    bool ok() const { return code == RuntimeControlCode::kOk; }

    RuntimeCommandStatus& ok(const std::string &msg = "success")
    {
        this->code = RuntimeControlCode::kOk;
        this->message = msg.empty() ? "success" : msg;
        this->runtime_err.set(RuntimeError::kOk);
        return *this;
    }

    RuntimeCommandStatus& failed(RuntimeControlCode code, RuntimeError err, const std::string &msg = "command failed")
    {
        this->code = code;
        this->message = msg.empty() ? "command failed" : msg;
        this->runtime_err = err;
        return *this;
    }

    static RuntimeCommandStatus Ok(const std::string &msg = "success")
    {
        RuntimeCommandStatus s;
        s.ok(msg);
        return s;
    }

    static RuntimeCommandStatus Failed(RuntimeControlCode code, RuntimeError err, const std::string &msg = "command failed")
    {
        RuntimeCommandStatus s;
        s.failed(code, err, msg);
        return s;
    }
};


/**
 * @brief 写操作回执：回答“本次写操作目标在 DB/runtime 中是否真实生效”。
 */
struct RuntimeMutationReceipt
{
    int32_t persisted{0};
    int32_t runtime_applied{0};

    static RuntimeMutationReceipt AllOk() { return RuntimeMutationReceipt{1, 1}; }
    static RuntimeMutationReceipt AllErr() { return RuntimeMutationReceipt{0, 0}; }
    static RuntimeMutationReceipt PersistedOk() { return RuntimeMutationReceipt{1, 0}; }
    static RuntimeMutationReceipt RuntimeOk() { return RuntimeMutationReceipt{0, 1}; }
};


/**
 * @brief 测试服务运行态快照
 */
struct ProjectRuntimeSnapshot
{
    int64_t project_id{0};
    ProjectRuntimeState runtime_state{ProjectRuntimeState::kStopped};
    uint16_t listen_port{0};
};

struct ProjectRuntimeResult
{
    RuntimeCommandStatus status;
    RuntimeMutationReceipt receipt;
    ProjectRuntimeSnapshot snapshot;

    bool ok() const { return status.ok(); }

    static ProjectRuntimeResult Success(RuntimeMutationReceipt receipt,
        ProjectRuntimeSnapshot snapshot,
        const std::string& msg = "success")
    {
        ProjectRuntimeResult r;
        r.status = RuntimeCommandStatus::Ok(msg);
        r.receipt = std::move(receipt);
        r.snapshot = std::move(snapshot);
        return r;
    }

    static ProjectRuntimeResult Failed(RuntimeControlCode code,
        RuntimeError err = RuntimeError(RuntimeError::kInternalError),
        const std::string& msg = "command failed",
        RuntimeMutationReceipt receipt = RuntimeMutationReceipt{},
        ProjectRuntimeSnapshot snapshot = ProjectRuntimeSnapshot{})
    {
        ProjectRuntimeResult r;
        r.status = RuntimeCommandStatus::Failed(code, std::move(err), msg);
        r.receipt = std::move(receipt);
        r.snapshot = std::move(snapshot);
        return r;
    }

};

/************ recover流程 批处理使用 ***********/
struct ProjectRecoverItemResult
{
    RuntimeCommandStatus status;
    ProjectRuntimeSnapshot snapshot;

    bool ok() const { return status.ok(); }
};

struct RuntimeRecoverResult
{
    RuntimeCommandStatus status;
    int32_t recovered_count{0};
    int32_t failed_count{0};
    std::vector<ProjectRecoverItemResult> runtime_projects;

    bool ok() const { return status.ok(); }
};
/************ recover流程 批处理使用 ***********/


struct ProtocolRuntimeSnapshot
{
    int64_t project_id{0};
    int64_t protocol_id{0};
    ProtocolConfigState config_state{ProtocolConfigState::kOff};
};

struct ProtocolRuntimeResult
{
    RuntimeCommandStatus status;
    RuntimeMutationReceipt receipt;
    ProtocolRuntimeSnapshot snapshot;

    bool ok() const { return status.ok(); }

    static ProtocolRuntimeResult Success(RuntimeMutationReceipt receipt,
        ProtocolRuntimeSnapshot snapshot,
        const std::string& msg = "success")
    {
        return {RuntimeCommandStatus::Ok(msg), std::move(receipt), std::move(snapshot)};
    }

    static ProtocolRuntimeResult Failed(RuntimeControlCode code,
        RuntimeError err = RuntimeError(RuntimeError::kInternalError),
        const std::string& msg = "command failed",
        RuntimeMutationReceipt receipt = RuntimeMutationReceipt{},
        ProtocolRuntimeSnapshot snapshot = ProtocolRuntimeSnapshot{})
    {
        return {RuntimeCommandStatus::Failed(code, std::move(err), msg), std::move(receipt), std::move(snapshot)};
    }

};

struct RuntimeOperationOptions
{
    int32_t timeout_ms{3000};
    bool record_trace{true};
};

class RuntimeControllerInterface
{
public:
    virtual ~RuntimeControllerInterface() = default;

    virtual void shutdown() = 0;

    /** project运行态操作 **/
    virtual ProjectRuntimeResult startProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;
    virtual ProjectRuntimeResult stopProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;
    virtual ProjectRuntimeResult delProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) = 0;
    virtual RuntimeRecoverResult recover(kit_muduo::HttpContextPtr ctx = nullptr) = 0;
    virtual ProjectRuntimeResult editPatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json &pattern_info) = 0;

    /** protocol运行态操作 **/
    virtual ProtocolRuntimeResult addProtocol(kit_muduo::HttpContextPtr ctx,  Protocol &p) = 0;
    virtual ProtocolRuntimeResult enableProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id) = 0;
    virtual ProtocolRuntimeResult disableProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id) = 0;
    virtual ProtocolRuntimeResult delProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id) = 0;
    virtual ProtocolRuntimeResult updateProtocolCfg(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, const nlohmann::json &patch) = 0;
    virtual ProtocolRuntimeResult updateProtocolBody(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;
    virtual ProtocolRuntimeResult reconfigProtocol(kit_muduo::HttpContextPtr ctx, Protocol &p) = 0;


    virtual std::shared_ptr<ProjectServer> findServer(int64_t project_id) = 0;
    virtual void addServer(int64_t project_id, std::shared_ptr<ProjectServer> server) = 0;
    virtual void removeServer(int64_t project_id) = 0;

};


class ProjectRuntimeManager
    :kit_muduo::Noncopyable
    ,public RuntimeControllerInterface
{
public:
    struct ProjectRuntimeRecord
    {
        int64_t project_id{0};
        ProjectRuntimeState runtime_state{ProjectRuntimeState::kStopped};
        uint16_t listen_port{0};
        std::shared_ptr<ProjectServer> server;
    };

    ProjectRuntimeManager(std::shared_ptr<ProjectSvcInterface> project_svc,
        std::shared_ptr<ProtocolSvcInterface> protocol_svc,
        size_t runtime_loop_capacity = 100);

    ~ProjectRuntimeManager() override = default;

    void shutdown() override;

    ProjectRuntimeResult startProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;
    ProjectRuntimeResult stopProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;
    ProjectRuntimeResult delProject(kit_muduo::HttpContextPtr ctx, int64_t project_id) override;
    RuntimeRecoverResult recover(kit_muduo::HttpContextPtr ctx = nullptr) override;
    ProjectRuntimeResult editPatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json &pattern_info) override;

    ProtocolRuntimeResult addProtocol(kit_muduo::HttpContextPtr ctx,  Protocol &p) override;
    ProtocolRuntimeResult enableProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id) override;
    ProtocolRuntimeResult disableProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id) override;
    ProtocolRuntimeResult delProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id) override;
    ProtocolRuntimeResult updateProtocolCfg(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, const nlohmann::json &patch) override;
    ProtocolRuntimeResult updateProtocolBody(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char> &body_data) override;
    ProtocolRuntimeResult reconfigProtocol(kit_muduo::HttpContextPtr ctx, Protocol &p) override;

    std::shared_ptr<ProjectServer> findServer(int64_t project_id) override;
    void addServer(int64_t project_id, std::shared_ptr<ProjectServer> server) override;
    void removeServer(int64_t project_id) override;

private:
    std::shared_ptr<std::mutex> lockForProject(int64_t project_id);

    template<typename FuncType>
    auto submitProjectOperation(int64_t project_id,
            RuntimeOperationKind op_kind,
            const char* command_name,
            RuntimeOperationOptions options,
            FuncType &&func) -> std::invoke_result_t<FuncType>
    {
        // 第一版快速上线实现：command_name/options 只作为日志和后续迁移预留。
        // 后续替换 Command Dispatcher 时，public API 和 *Impl 不需要改。
        (void)op_kind;
        (void)command_name;
        (void)options;
        return runPorjectLocked(project_id, std::forward<FuncType>(func));
    }

    template<typename FuncType>
    auto runPorjectLocked(int64_t project_id, FuncType &&func) -> std::invoke_result_t<FuncType>
    {
        auto mtx = lockForProject(project_id);
        std::unique_lock<std::mutex> lock(*mtx);
        return std::invoke(std::forward<FuncType>(func));
    }

    ProjectRuntimeResult startProjectImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id);
    ProjectRuntimeResult stopProjectImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id);
    ProjectRuntimeResult delProjectImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id);
    ProjectRecoverItemResult  recoverProjectImpl(kit_muduo::HttpContextPtr ctx, const Project& p);
    ProjectRuntimeResult  editPatternInfoImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json &pattern_info);

    ProjectRuntimeResult createAndStartProjectServerImpl(kit_muduo::HttpContextPtr ctx, const Project& p);

    RuntimeResult<void> registerRuntimeEnabledProtocolsLocked(kit_muduo::HttpContextPtr ctx,
        const std::shared_ptr<ProjectServer> &project_server,
        int64_t project_id,
        ProtocolType type);

    ProtocolRuntimeResult addProtocolImpl(kit_muduo::HttpContextPtr ctx,  Protocol &p);
    ProtocolRuntimeResult enableProtocolImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id);
    ProtocolRuntimeResult disableProtocolImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id);
    ProtocolRuntimeResult delProtocolImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id);
    ProtocolRuntimeResult updateProtocolCfgImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, const nlohmann::json &patch);
    ProtocolRuntimeResult updateProtocolBodyImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char> &body_data);
    ProtocolRuntimeResult reconfigProtocolImpl(kit_muduo::HttpContextPtr ctx, Protocol &p);

private:
    std::shared_ptr<ProjectSvcInterface> project_svc_;
    std::shared_ptr<ProtocolSvcInterface> protocol_svc_;
    /// @brief 全局事件循坏池
    RuntimeLoopPool loop_pool_;

    /// @brief 全局测试服务器容器 锁
    std::mutex register_mtx_;
    /// @brief 全局测试服务器容器 用于后台-测试服务器通信
    std::unordered_map<int64_t, ProjectRuntimeRecord> runtime_projects_;

    std::mutex locks_mtx_;
    /// @brief 全局测试服务器-按项目串行化锁集合
    std::unordered_map<int64_t, std::shared_ptr<std::mutex>> project_locks_;

};


}
#endif //__KIT_RUNTIME_CONTROLLER_H__
