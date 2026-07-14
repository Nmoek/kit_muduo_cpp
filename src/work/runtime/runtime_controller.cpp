/**
 * @file runtime_controller.cpp
 * @brief 运行态业务管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-08 15:20:24
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/custom_tcp_pattern.h"
#include "domain/domain_log.h"
#include "domain/protocol_body_pipeline.h"
#include "domain/protocol_config_pipeline.h"
#include "domain/runtime_result.h"
#include "domain/type.h"
#include "runtime/runtime_log.h"
#include "runtime/runtime_controller.h"
#include "service/svc_project.h"
#include "service/svc_protocol.h"
#include "domain/project_server.h"
#include "domain/project_server_factory.h"
#include "domain/protocol_item.h"
#include "domain/protocol.h"
#include "domain/custom_tcp_pattern_spec.h"
#include "domain/protocol_interaction_publisher.h"

#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <unistd.h>

using namespace kit_muduo;

namespace kit_domain {


namespace {

inline ProtocolRuntimeResult CheckProtocolRuntimeCondition(const ProtocolAccessInfo &access_info, bool acquire_running, bool server_null)
{
    if(ProjectStatus::kValid != access_info.project_status)
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kProjectDeleted, RuntimeError(RuntimeError::kInternalError),
        "project deleted");
    }
    if(ProtocolStatus::kValid != access_info.protocol_status)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kProtocolDeleted, RuntimeError(RuntimeError::kInternalError),
        "protocol deleted");
    }

    if(acquire_running)
    {
        if(ProjectRuntimeState::kRunning != access_info.project_runtime_state
            || server_null)
        {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, RuntimeError(RuntimeError::kInternalError),
            "project not running");
        }
    }

    return ProtocolRuntimeResult::Success(RuntimeMutationReceipt{}, ProtocolRuntimeSnapshot{});
}

}

ProjectRuntimeManager::ProjectRuntimeManager(std::shared_ptr<ProjectSvcInterface> project_svc,
    std::shared_ptr<ProtocolSvcInterface> protocol_svc,
    std::shared_ptr<ProtocolInteractionPublisher> publisher,
    size_t runtime_loop_capacity)
    :project_svc_(std::move(project_svc))
    ,protocol_svc_(std::move(protocol_svc))
    ,loop_pool_(runtime_loop_capacity)
    ,publisher_(publisher)
{


}

void ProjectRuntimeManager::shutdown()
{
    std::vector<std::shared_ptr<ProjectServer>> servers;
    {
        std::unique_lock<std::mutex> lock(register_mtx_);
        servers.reserve(runtime_projects_.size());
        for(auto &it : runtime_projects_)
        {
            if(it.second.server)
            {
                servers.push_back(it.second.server);
            }
        }
        runtime_projects_.clear();
    }

    for(auto &server : servers)
    {
        if(!server->stop())
        {
            RUNPJMA_F_WARN("runtime server stop failed during shutdown! pjId[%ld]\n", server->getProjectId());
        }
    }

    loop_pool_.shutdown();
}

#define RUNTIME_OP(OP_KIND, OP_FUNC, ...) \
    submitProjectOperation(project_id, \
        RuntimeOperationKind::OP_KIND, \
        #OP_FUNC, \
        RuntimeOperationOptions{3000, true}, \
        std::bind(&ProjectRuntimeManager::OP_FUNC##Impl, this, ctx, ##__VA_ARGS__))


ProjectRuntimeResult ProjectRuntimeManager::startProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return RUNTIME_OP(kStartProject, startProject, project_id);
}

ProjectRuntimeResult ProjectRuntimeManager::stopProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return RUNTIME_OP(kStopProject,stopProject, project_id);
}

ProjectRuntimeResult ProjectRuntimeManager::delProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return RUNTIME_OP(kDeleteProject,delProject, project_id);
}

RuntimeRecoverResult ProjectRuntimeManager::recover(kit_muduo::HttpContextPtr ctx)
{

    RuntimeRecoverResult result;
    auto active_pjs = project_svc_->GetAllActive(ctx);

    std::vector<int64_t> failed_pj_ids;
    for(auto &p : active_pjs)
    {
        const int64_t project_id = p.m_id;
        auto item_result = RUNTIME_OP(kRecoverProject, recoverProject, (std::move(p)));
        if(!item_result.ok())
        {
            failed_pj_ids.push_back(project_id);
            ++result.failed_count;
        }
        else
        {
            ++result.recovered_count;
        }
        result.runtime_projects.push_back(std::move(item_result));
    }

    if(result.failed_count > 0)
    {
        // rcover失败的project都进行回滚

        result.status = RuntimeCommandStatus::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError));
    }

    return result;
}


ProjectRuntimeResult ProjectRuntimeManager::editPatternInfo(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json &pattern_info)
{
    return RUNTIME_OP(kEditPatternInfo, editPatternInfo, project_id, pattern_info);
}

ProtocolRuntimeResult ProjectRuntimeManager::addProtocol(kit_muduo::HttpContextPtr ctx, Protocol &p)
{
    int64_t project_id = p.m_projectId;
    return RUNTIME_OP(kAddProtocol, addProtocol, std::ref(p));
}

ProtocolRuntimeResult ProjectRuntimeManager::enableProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id)
{
    return RUNTIME_OP(kEnableProtocol, enableProtocol, project_id, protocol_id);
}

ProtocolRuntimeResult ProjectRuntimeManager::disableProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id)
{
    return RUNTIME_OP(kDisableProtocol, disableProtocol, project_id, protocol_id);
}

ProtocolRuntimeResult ProjectRuntimeManager::delProtocol(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id)
{
    return RUNTIME_OP(kDeleteProtocol, delProtocol, project_id, protocol_id);
}

ProtocolRuntimeResult ProjectRuntimeManager::updateProtocolCfg(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, const nlohmann::json &patch)
{
    return RUNTIME_OP(kUpdateProtocolCfg, updateProtocolCfg, 
        project_id, protocol_id, side, patch);
}

ProtocolRuntimeResult ProjectRuntimeManager::updateProtocolBody(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    return RUNTIME_OP(kUpdateProtocolBody, updateProtocolBody, 
        project_id, protocol_id, side, body_type, std::ref(body_data));
}

ProtocolRuntimeResult ProjectRuntimeManager::reconfigProtocol(kit_muduo::HttpContextPtr ctx, Protocol &p) 
{
    int64_t project_id = p.m_projectId;
    return RUNTIME_OP(kReconfigProtocol, reconfigProtocol, std::ref(p));
}

#undef RUNTIME_OP


std::shared_ptr<ProjectServer> ProjectRuntimeManager::findServer(int64_t project_id)
{
    std::unique_lock<std::mutex> lock(register_mtx_);
    auto it = runtime_projects_.find(project_id);
    return it == runtime_projects_.end() ? nullptr : it->second.server;
}

void ProjectRuntimeManager::addServer(int64_t project_id, std::shared_ptr<ProjectServer> server)
{
    std::unique_lock<std::mutex> lock(register_mtx_);
    auto it = runtime_projects_.find(project_id);
    if(it != runtime_projects_.end())
    {
        return;
    }

    runtime_projects_.emplace(project_id,
        ProjectRuntimeRecord{
        .project_id = project_id,
        .runtime_state = ProjectRuntimeState::kRunning,
        .listen_port = server->getBindAddr().toPort(),
        .server = std::move(server),
    });

    std::unique_lock<std::mutex> lock2(locks_mtx_);
    project_locks_.emplace(project_id, std::make_shared<std::mutex>());
}

void ProjectRuntimeManager::removeServer(int64_t project_id)
{
    std::unique_lock<std::mutex> lock(register_mtx_);
    auto it = runtime_projects_.find(project_id);
    if(it == runtime_projects_.end())
    {
        return;
    }
    runtime_projects_.erase(project_id);
}

std::shared_ptr<std::mutex> ProjectRuntimeManager::lockForProject(int64_t project_id)
{
    std::unique_lock<std::mutex> lock(locks_mtx_);
    auto it = project_locks_.find(project_id);
    if(it == project_locks_.end())
    {
        auto mtx = std::make_shared<std::mutex>();
        project_locks_.emplace(project_id, mtx);
        return mtx;
    }
    return it->second;
}


ProjectRuntimeResult ProjectRuntimeManager::startProjectImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    const auto &p = project_svc_->GetById(ctx, project_id);
    if(p.m_id <= 0)
    {
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kProjectNotFound);
    }
    if(ProjectStatus::kInvalid == p.m_status)
    {
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kProjectDeleted);
    }

    return createAndStartProjectServerImpl(ctx, p);
}


ProjectRuntimeResult ProjectRuntimeManager::stopProjectImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    ProjectRuntimeResult result;

    std::unique_lock<std::mutex> lock(register_mtx_);
    auto it = runtime_projects_.find(project_id);

    if(it == runtime_projects_.end())
    {
        if(!project_svc_->UpdateRuntimeState(ctx, project_id, ProjectRuntimeState::kStopped, 0))
        {
            RUNPJMA_F_ERROR("UpdateRuntimeState error! pjId[%ld]\n", project_id);
            return ProjectRuntimeResult::Failed(RuntimeControlCode::kPersistFailed);
        }
        return ProjectRuntimeResult::Success(RuntimeMutationReceipt::AllOk(),
            ProjectRuntimeSnapshot{
                .project_id = project_id,
                .runtime_state = ProjectRuntimeState::kStopped,
                .listen_port = 0
            });
    }
    auto project_server = it->second.server;
    lock.unlock();

    // 实际停止runtime
    if(project_server->stop())
    {
        removeServer(project_id);
    }
    else
    {
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed);
    }

    if(!project_svc_->UpdateRuntimeState(ctx, project_id, ProjectRuntimeState::kStopped, 0))
    {
        RUNPJMA_F_ERROR("UpdateRuntimeState error! pjId[%ld]\n", project_id);
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kPersistFailed);
    }

    // 清理协议交互详情数据
    // project_server->clearProjectInteraction(project_id);

    return ProjectRuntimeResult::Success(RuntimeMutationReceipt::AllOk(),
    ProjectRuntimeSnapshot{
        .project_id = project_id,
        .runtime_state = ProjectRuntimeState::kStopped,
        .listen_port = 0
    });
}

ProjectRuntimeResult ProjectRuntimeManager::delProjectImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{

    ProjectRuntimeResult result = stopProjectImpl(ctx, project_id);
    if(!result.ok())
    {
        return result;
    }

    if(!project_svc_->UpdateStatus(ctx, project_id, ProjectStatus::kInvalid))
    {
        RUNPJMA_F_ERROR("UpdateStatus-> invalid error! pjId[%ld]\n", project_id);

        return ProjectRuntimeResult::Failed(RuntimeControlCode::kPersistFailed);
    }

    return ProjectRuntimeResult::Success(RuntimeMutationReceipt::AllOk(),
    ProjectRuntimeSnapshot{
        .project_id = project_id,
        .runtime_state = ProjectRuntimeState::kStopped,
        .listen_port = 0,
    });
}

ProjectRecoverItemResult  ProjectRuntimeManager::recoverProjectImpl(kit_muduo::HttpContextPtr ctx, const Project& p)
{
    auto presult = createAndStartProjectServerImpl(ctx, p);
    auto snapshot = std::move(presult.snapshot);
    if(snapshot.project_id <= 0)
    {
        snapshot.project_id = p.m_id;
    }

    return ProjectRecoverItemResult{
        .status = std::move(presult.status),
        .snapshot = std::move(snapshot),
    };
}

ProjectRuntimeResult ProjectRuntimeManager::editPatternInfoImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, const nlohmann::json &pattern_info)
{
    if(project_id <= 0)
    {
        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kInvalidArgument,
            RuntimeError(RuntimeError::kInvalidArgument),
            "request param invalid");
    }

    const auto &p = project_svc_->GetById(ctx, project_id);
    if(p.m_id <= 0)
    {
        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kProjectNotFound,
            RuntimeError(RuntimeError::kInvalidArgument),
            "project not found");
    }
    if(ProjectStatus::kInvalid == p.m_status)
    {
        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kProjectDeleted,
            RuntimeError(RuntimeError::kInvalidArgument),
            "project deleted");
    }
    if(ProtocolType::kCustomTcp != p.m_protocolType)
    {
        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kProjectTypeInvalid,
            RuntimeError(RuntimeError::kInvalidArgument),
            "project type invalid");
    }
    if(ProjectRuntimeState::kRunning == p.m_runtimeState || nullptr != findServer(project_id))
    {
        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kInvalidArgument,
            RuntimeError(RuntimeError::kInvalidArgument),
            "请先停止测试服务后再修改格式信息");
    }

    if(!CustomTcpPatternSpec::FromJson(pattern_info).has_value())
    {
        RUNPJMA_F_ERROR(" pattern_info invalid\n");

        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kInvalidArgument,
            RuntimeError(RuntimeError::kInvalidArgument),
            "pattern info invalid");
    }

    if(!project_svc_->UpdatePatternInfoWithProtocolWithdraw(ctx, project_id, pattern_info))
    {
        return ProjectRuntimeResult::Failed(
            RuntimeControlCode::kPersistFailed,
            RuntimeError(RuntimeError::kInternalError),
            "service failed");
    }
    return ProjectRuntimeResult::Success(RuntimeMutationReceipt::PersistedOk(), ProjectRuntimeSnapshot{
        .project_id = project_id,
        .runtime_state = ProjectRuntimeState::kStopped,
        .listen_port = 0,
    });
}

ProjectRuntimeResult ProjectRuntimeManager::createAndStartProjectServerImpl(kit_muduo::HttpContextPtr ctx, const Project& p)
{
    int64_t project_id = p.m_id;
    std::unique_lock<std::mutex> lock(register_mtx_);

    auto it = runtime_projects_.find(p.m_id);
    if(it != runtime_projects_.end()
        && ProjectRuntimeState::kRunning == it->second.runtime_state)
    {
        return ProjectRuntimeResult::Success(RuntimeMutationReceipt::AllOk(),
            ProjectRuntimeSnapshot{
                .project_id = p.m_id,
                .runtime_state = ProjectRuntimeState::kRunning,
                .listen_port = it->second.listen_port
            });
    }
    lock.unlock();

    if(!CheckProjectMode(p.m_mode)
        || !CheckProtocolType(p.m_protocolType))
    {
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kProjectTypeInvalid);
    }

    // 如果当前数据库时已开启状态 置为未开启
    if(ProjectRuntimeState::kRunning == p.m_runtimeState)
    {
        if(!project_svc_->UpdateRuntimeState(ctx, project_id, ProjectRuntimeState::kStopped, 0))
        {
            RUNPJMA_F_WARN("UpdateRuntimeState error! pjId[%ld] \n", project_id);
        }
    }

    // 获取loop租约
    auto lease_result = loop_pool_.acquire(project_id);
    if(!lease_result.ok() || !lease_result.val)
    {
        RUNPJMA_F_ERROR("runtime loop lease falid: %s\n", lease_result.error.toMsg().c_str());

        return ProjectRuntimeResult::Failed(RuntimeControlCode::kLoopLeaseFailed, lease_result.error);
    }
    std::shared_ptr<RuntimeLease> lease_loop = lease_result.val;

    // 工厂模式创建测试服务
    auto project_server = ProjectServerFactory::Create(p, lease_loop);
    if (!project_server)
    {
        RUNPJMA_F_ERROR("create ProjectServer faild! pjId[%ld], type[%d] \n", project_id, static_cast<int32_t>(p.m_protocolType));

        return ProjectRuntimeResult::Failed(RuntimeControlCode::kCreateServerFailed);
    }


    auto runtime_enabled_pcs_result = registerRuntimeEnabledProtocolsLocked(ctx, project_server, project_id, p.m_protocolType);
    if(!runtime_enabled_pcs_result.ok())
    {
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kCreateProtocolItemFailed, std::move(runtime_enabled_pcs_result.error));
    }

    uint16_t cur_listen_port = project_server->getBindAddr().toPort();

    std::weak_ptr<ProtocolInteractionPublisher> weak_publisher(publisher_);
    // 协议交互详情发布器器 挂载
    project_server->setObserveCallback([weak_publisher](const ProtocolInteractionObservation &obs){
        auto publisher = weak_publisher.lock()
        ;
        if(!publisher)
        {
            RUNPJMA_F_INFO("protocol interaction publisher null\n");
            return;
        }
        publisher->publish(std::move(obs));
    });

    // 协议项全部挂载成功后 开启监听
    project_server->start();

    addServer(project_id, project_server);

    if(!project_svc_->UpdateRuntimeState(ctx, project_id, ProjectRuntimeState::kRunning, cur_listen_port))
    {
        RUNPJMA_F_ERROR("UpdateRuntimeState error! pjId[%ld] \n", project_id);

        project_server->stop();
        removeServer(project_id);

        return ProjectRuntimeResult::Failed(RuntimeControlCode::kPersistFailed);
    }

    return ProjectRuntimeResult::Success(RuntimeMutationReceipt::AllOk(),
    ProjectRuntimeSnapshot{
        .project_id = project_id,
        .runtime_state = ProjectRuntimeState::kRunning,
        .listen_port = cur_listen_port
    });
}


RuntimeResult<void> ProjectRuntimeManager::registerRuntimeEnabledProtocolsLocked(kit_muduo::HttpContextPtr ctx,
        const std::shared_ptr<ProjectServer> &project_server,
        int64_t project_id,
        ProtocolType type)
{
    RuntimeResult<void> result;
    std::shared_ptr<CustomTcpPattern> pattern = nullptr;

    if(ProtocolType::kCustomTcp == type)
    {
        pattern = CustomTcpPatternFactory::Create(project_svc_->GetPatternInfoById(ctx, project_id));
        if(!pattern)
        {
            result.error.set(RuntimeError::kInvalidProtocolConfig);
            return result;
        }
    }

    // 查询当前测试服务上所有`未软删且上线状态`的协议项
    // 当前策略：只要失败一项就报错返回
    std::vector<Protocol> active_pcs = protocol_svc_->GetActiveByProject(ctx, project_id);
    for(auto &pc : active_pcs)
    {

        auto build_result = ProtocolConfigPipeline::buildItem({
            .full_config = ProtocolFullConfigSpec{
                .type = pc.m_type,
                .req_cfg = pc.m_reqCfg,
                .resp_cfg = pc.m_respCfg,
            },
            .ori_protocol = pc,
            .custom_tcp_pattern = pattern,
        });
        if(!build_result.ok || !build_result.runtime_key.has_value())
        {
            RUNTIME_F_ERROR("protocol item create failed! pjId[%ld], pcId[%ld], type[%d]\n", pc.m_projectId, pc.m_id, static_cast<int32_t>(pc.m_type));
            result.error.set(RuntimeError::kInvalidProtocolConfig);
            return result;
        }
        const std::string& build_key = build_result.runtime_key.value();
        // 检查runtime_key进行对账(防止人工改库 以及旧版本数据干扰)
        if(build_key != pc.m_runtimeKey)
        {
            // 有问题的协议回滚为重配置状态

            if(!protocol_svc_->UpdateConfigState(ctx, pc.m_id, ProtocolConfigState::kReConfig))
            {
                RUNTIME_F_ERROR("protocol item reconfig rollback error! pjId[%ld], pcId[%ld], type[%d]\n",  
                        project_id, pc.m_id, static_cast<int32_t>(pc.m_type));
                result.error.set(RuntimeError::kInvalidProtocolConfig);
                return result;
            }

            RUNTIME_F_WARN("protocol item runtime_key mismatch! pjId[%ld], pcId[%ld], type[%d]: db[%s] <--> runtime[%s]\n", 
                project_id, pc.m_id, static_cast<int32_t>(pc.m_type),
                pc.m_runtimeKey.c_str(), build_key.c_str());

            continue;
        }

        result = InvokeOnLoopSync(project_server->getLoop(), 3000, [project_server,
            protocol_item = build_result.item](){
            return project_server->AddProtocolItem(protocol_item);
        });
        if(!result.ok())
        {
            RUNPJMA_F_ERROR(" ProtocolItem add faild!  pjId[%ld], pcId[%ld], type[%d]\n", pc.m_projectId, pc.m_id, static_cast<int32_t>(pc.m_type));

            return result;
        }

        RUNPJMA_F_DEBUG(" ProtocolItem create/add success!  pjId[%ld], pcId[%ld], type[%d]\n", pc.m_projectId, pc.m_id, static_cast<int32_t>(pc.m_type));
    }
    return result;
}


ProtocolRuntimeResult ProjectRuntimeManager::addProtocolImpl(kit_muduo::HttpContextPtr ctx, Protocol &p)
{
    const auto& pj = project_svc_->GetById(ctx, p.m_projectId);
    if(pj.m_id <= 0 || ProjectStatus::kValid != pj.m_status)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kProjectNotFound, RuntimeError(RuntimeError::kInternalError),
        "project not found");
    }
    int64_t project_id = pj.m_id;

    // 注意: 重配置状态用户不可以配置
    if(ProtocolConfigState::kOff != p.m_configState
        && ProtocolConfigState::kOn != p.m_configState)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "project config_state invliad");
    }

    if((p.m_type <= ProtocolType::kUnknown || p.m_type >= ProtocolType::kMax)
        || p.m_type != pj.m_protocolType)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kProjectTypeInvalid, RuntimeError(RuntimeError::kInternalError),
        "project type invliad");
    }

    // body格式检查
    auto body_check = ProtocolBodyPipeline::CheckFullProtocol(p);
    if(!body_check.ok)
    {
        RUNTIME_F_ERROR("protocol body check invalid: %s\n", body_check.message.c_str());
        return ProtocolRuntimeResult::Failed(
            RuntimeControlCode::kInvalidArgument,
            RuntimeError(RuntimeError::kInternalError),
            body_check.message);
    }

    auto pj_server = findServer(project_id);

    if(ProtocolConfigState::kOn == p.m_configState)
    {
        if(ProjectRuntimeState::kRunning != pj.m_runtimeState
            || !pj_server)
        {
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
                RuntimeError(RuntimeError::kInternalError),
                "project not running");
        }
    }

    std::shared_ptr<CustomTcpPattern> pattern = nullptr;

    // 注意: 需要上线且是TCP再做格式信息处理
    if(ProtocolType::kCustomTcp == p.m_type)
    {
        pattern = CustomTcpPatternFactory::Create(project_svc_->GetPatternInfoById(ctx, project_id));
        if(!pattern)
        {
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
            "pattern info invalid");
        }

    }


    auto build_result = ProtocolConfigPipeline::buildItem({
        .full_config = ProtocolFullConfigSpec{
            .type = p.m_type,
            .req_cfg = p.m_reqCfg,
            .resp_cfg = p.m_respCfg,
        },
        .ori_protocol = p,
        .custom_tcp_pattern = pattern,
    });
    if(!build_result.ok || !build_result.runtime_key.has_value())
    {

        RUNTIME_F_ERROR("create protocol item error! pjId[%ld], type[%d], name[%s]: %s\n", p.m_projectId, static_cast<int32_t>(p.m_type), p.m_name.c_str(), build_result.message.c_str());

       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kCreateProtocolItemFailed, 
        RuntimeError(RuntimeError::kInternalError),
        build_result.message);
    }
    // 运行键更新一下
    p.m_runtimeKey = build_result.runtime_key.value();

    auto item = build_result.item;

    int64_t protocol_id = protocol_svc_->Add(ctx, p);
    if(protocol_id <= 0)
    {
        RUNTIME_F_ERROR("protocol add error! pjId[%ld], type[%d], name[%s]\n", p.m_projectId, static_cast<int32_t>(p.m_type), p.m_name.c_str());

       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError),
        "protocol insert db error");
    }
    p.m_id = protocol_id;
    build_result.item->setId(protocol_id);
    RuntimeMutationReceipt receipt;
    // DB更新成功
    receipt.persisted = 1;


    if(ProtocolConfigState::kOn == p.m_configState)
    {

        auto runtime_result = InvokeOnLoopSync(pj_server->getLoop(), 1000, [pj_server, item](){
            return pj_server->AddProtocolItem(item);
        });
        if(!runtime_result.ok())
        {
            RUNTIME_F_ERROR("protocol_item runtime add error! pjId[%ld], pcId[%ld], type[%d]: %s\n",
                p.m_projectId,
                protocol_id,
                static_cast<int32_t>(p.m_type),
                runtime_result.error.toMsg().c_str());

            if(!protocol_svc_->Del(ctx, protocol_id))
            {
                RUNTIME_F_ERROR("protocol_item  del-rollback  error! pjId[%ld], pcId[%ld], type[%d]\n",
                    p.m_projectId,
                    protocol_id,
                    static_cast<int32_t>(p.m_type));
                // 回滚失败
                return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeRollbackFailed, 
                    runtime_result.error,
                    runtime_result.error.toMsg(),
                    RuntimeMutationReceipt::PersistedOk());
            }
            else
            {
                receipt.persisted = 0;
            }

            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
                runtime_result.error,
                "protocol_item runtime add error",
                receipt);        
        }
        receipt.runtime_applied = 1;
    }


    return ProtocolRuntimeResult::Success(receipt, ProtocolRuntimeSnapshot{
        .project_id = p.m_projectId,
        .protocol_id = protocol_id,
        .config_state = p.m_configState
    });
}

ProtocolRuntimeResult ProjectRuntimeManager::delProtocolImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id)
{

    ProtocolAccessInfo access_info;
    if(!protocol_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError),
        "get access info failed");
    }

    if(access_info.project_id != project_id)
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol project mismatch");
    }
    auto pj_server = findServer(project_id);

    // 注意：这里运行态是否启动都可以删除
    auto result = CheckProtocolRuntimeCondition(access_info, 
        ProjectRuntimeState::kRunning == access_info.project_runtime_state, pj_server == nullptr);
    if(!result.ok())
    {
        return result;
    }
    RuntimeMutationReceipt receipt;

    if(!protocol_svc_->Del(ctx, protocol_id))
    {
        RUNTIME_F_ERROR("protocol del db error! pjId[%ld], pcId[%ld]\n", project_id, protocol_id);

       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError),
        "protocol del db error");
    }
    receipt.persisted = 1;

    //注意: 运行态存在且协议是上线状态 需要清理运行态
    if(ProjectRuntimeState::kRunning == access_info.project_runtime_state
        && ProtocolConfigState::kOn == access_info.protocol_config_state)
    {

        auto runtime_result = InvokeOnLoopSync(pj_server->getLoop(), 1000, [pj_server, protocol_id](){
            return pj_server->DelProtocolItem(protocol_id);
        });
        if(!runtime_result.ok())
        {
            RUNTIME_F_ERROR("protocol_item runtime del error! pjId[%ld], pcId[%ld]: %s\n", project_id, protocol_id, runtime_result.error.toMsg().c_str());

            if(!protocol_svc_->ReCover(ctx, protocol_id))
            {
                RUNTIME_F_ERROR("protocol_item  del-rollback  error! pjId[%ld], pcId[%ld]\n", project_id, protocol_id);

                // 回滚失败
                return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeRollbackFailed, 
                    runtime_result.error,
                    runtime_result.error.toMsg(),
                    RuntimeMutationReceipt::PersistedOk());
            }
            else
            {
                receipt.persisted = 0;
            }

            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
                runtime_result.error,
                "protocol_item runtime del error",
                receipt);        
        }
        receipt.runtime_applied = 1;
    }

    // 清理协议项交互详情
    // pj_server->clearProtocolInteraction(project_id, protocol_id);

    return ProtocolRuntimeResult::Success(receipt, ProtocolRuntimeSnapshot{
        .project_id = project_id,
        .protocol_id = protocol_id,
        .config_state = ProtocolConfigState::kOff,
    });
}


ProtocolRuntimeResult ProjectRuntimeManager::enableProtocolImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id)
{
    ProtocolAccessInfo access_info;
    if(!protocol_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError),
        "get access info failed");
    }

    if(access_info.project_id != project_id)
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol project mismatch");
    }
    auto pj_server = findServer(project_id);

    auto result = CheckProtocolRuntimeCondition(access_info, true, pj_server == nullptr);
    if(!result.ok())
    {
        return result;
    }

    if(ProtocolConfigState::kReConfig == access_info.protocol_config_state)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, RuntimeError(RuntimeError::kInternalError),
        "需要重新配置协议项");
    }
    else if(ProtocolConfigState::kOn == access_info.protocol_config_state)
    {
        RUNTIME_F_INFO("protocol item enabled! pcId[%ld] pjId[%ld]\n", protocol_id, project_id);

       return ProtocolRuntimeResult::Success(RuntimeMutationReceipt::AllOk(), 
       ProtocolRuntimeSnapshot{
            .project_id = project_id,
            .protocol_id = protocol_id,
            .config_state = ProtocolConfigState::kOn
       });
    }

    const auto& p = protocol_svc_->GetById(ctx, protocol_id);
    if(p.m_id <= 0)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kProtocolNotFound, RuntimeError(RuntimeError::kInternalError),
        "protocol not exist");
    }

    auto build_result = ProtocolConfigPipeline::buildItem(ProtocolItemBuildSpec{
        .full_config = ProtocolFullConfigSpec{
            .type  = p.m_type,
            .req_cfg = p.m_reqCfg,
            .resp_cfg = p.m_respCfg
        },
        .ori_protocol = p,
        .custom_tcp_pattern =  pj_server->GetPatternInfo()
    });
    if(!build_result.ok)
    {
        RUNTIME_F_ERROR("create ProtocolItem faild! pjId[%ld], pcId[%ld], type[%d]  \n", project_id, protocol_id, static_cast<int32_t>(p.m_type));

        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kCreateProtocolItemFailed, RuntimeError(RuntimeError::kInternalError), build_result.message);
    }

    if(!protocol_svc_->UpdateConfigState(ctx, protocol_id, ProtocolConfigState::kOn))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError), "enable protocol failed");
    }

    auto runtime_result = InvokeOnLoopSync(pj_server->getLoop(), 1000, [pj_server, item = build_result.item](){
        return pj_server->AddProtocolItem(item);
    });
    if(!runtime_result.ok())
    {
        if(!protocol_svc_->UpdateConfigState(ctx, protocol_id, ProtocolConfigState::kOff))
        {
            RUNTIME_F_ERROR("protocol_item off-rollback  error! pjId[%ld], pcId[%ld]\n", project_id, protocol_id);
            // 回滚失败
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeRollbackFailed, 
                runtime_result.error,
                runtime_result.error.toMsg(),
                RuntimeMutationReceipt::PersistedOk());
        }
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
            runtime_result.error, 
            "protocol runtime apply failed");
    }

    return ProtocolRuntimeResult::Success(RuntimeMutationReceipt::AllOk(), ProtocolRuntimeSnapshot{
        .project_id = project_id,
        .protocol_id = protocol_id,
        .config_state = ProtocolConfigState::kOn
    });
}

ProtocolRuntimeResult ProjectRuntimeManager::disableProtocolImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id)
{
    ProtocolAccessInfo access_info;
    if(!protocol_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError),
        "get access info failed");
    }
    if(access_info.project_id != project_id)
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol project mismatch");
    }
    auto pj_server = findServer(project_id);

    auto result = CheckProtocolRuntimeCondition(access_info, true, pj_server == nullptr);
    if(!result.ok())
    {
        return result;
    }

    if(ProtocolConfigState::kReConfig == access_info.protocol_config_state)
    {
       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, RuntimeError(RuntimeError::kInternalError),
        "需要重新配置协议项");
    }
    else if(ProtocolConfigState::kOff == access_info.protocol_config_state)
    {
       return ProtocolRuntimeResult::Success(RuntimeMutationReceipt::AllOk(), 
       ProtocolRuntimeSnapshot{
            .project_id = project_id,
            .protocol_id = protocol_id,
            .config_state = ProtocolConfigState::kOff
       });
    }

    if(!protocol_svc_->UpdateConfigState(ctx, protocol_id, ProtocolConfigState::kOff))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError), "enable protocol failed");
    }

    auto runtime_result = InvokeOnLoopSync(pj_server->getLoop(), 1000, [pj_server, protocol_id](){
        return pj_server->DelProtocolItem(protocol_id);
    });
    if(!runtime_result.ok())
    {
        if(!protocol_svc_->UpdateConfigState(ctx, protocol_id, ProtocolConfigState::kOn))
        {
            RUNTIME_F_ERROR("protocol_item on-rollback  error! pjId[%ld], pcId[%ld]\n", project_id, protocol_id);
            // 回滚失败
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeRollbackFailed, 
                runtime_result.error,
                runtime_result.error.toMsg(),
                RuntimeMutationReceipt::PersistedOk());
        }
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
            runtime_result.error, 
            "protocol runtime apply failed");
    }

    return ProtocolRuntimeResult::Success(RuntimeMutationReceipt::AllOk(), ProtocolRuntimeSnapshot{
        .project_id = project_id,
        .protocol_id = protocol_id,
        .config_state = ProtocolConfigState::kOff
    });
}

ProtocolRuntimeResult ProjectRuntimeManager::updateProtocolCfgImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, const nlohmann::json &patch)
{
    ProtocolAccessInfo access_info;
    if(!protocol_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError),
        "get access info failed");
    }

    if(access_info.project_id != project_id
        || (ProtocolSide::kRequest != side && ProtocolSide::kResponse != side)
        || !patch.is_object())
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol project mismatch");
    }
    auto pj_server = findServer(project_id);

    auto result = CheckProtocolRuntimeCondition(access_info,  
        ProjectRuntimeState::kRunning == access_info.project_runtime_state, 
        pj_server == nullptr);
    if(!result.ok())
    {
        return result;
    }

    if(ProtocolConfigState::kReConfig == access_info.protocol_config_state)
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol need reconfig");
    }
    RuntimeMutationReceipt receipt;


    nljson old_all_cfg_json;
    nljson old_cfg_json;
    nljson new_cfg_json;

    // 1. 查出旧配置
    old_all_cfg_json = protocol_svc_->GetCfgById(ctx, protocol_id);

    // 2. 把局部新配置/全新配置和旧配置"并集"合并
    const char* cfg_key = ProtocolSide::kRequest == side ? "req_cfg" : "resp_cfg";
    old_cfg_json = new_cfg_json = old_all_cfg_json.at(cfg_key);
    new_cfg_json.merge_patch(patch);

    RUNTIME_F_DEBUG("cfg json merge: pjId[%d], pcId[%ld], cfg_key[%s]: %s\n", project_id, protocol_id, cfg_key, new_cfg_json.dump(4).c_str());

    std::optional<CustomTcpPatternSpec> pattern_spec;
    // 注意: 需要上线且是TCP再做格式信息处理
    if(ProtocolType::kCustomTcp == access_info.protocol_type)
    {
        pattern_spec = CustomTcpPatternSpec::FromJson(project_svc_->GetPatternInfoById(ctx, project_id));
        if(!pattern_spec.has_value())
        {
            RUNTIME_F_ERROR("pattern info invalid! pjId[%ld]\n", project_id);
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
            "pattern info invalid");
        }

    }

    auto check_result = ProtocolConfigPipeline::checkConfig({
        .type = access_info.protocol_type,
        .side = side,
        .cfg = new_cfg_json,
        .custom_tcp_pattern_spec = std::move(pattern_spec),
    });
    if(!check_result.ok)
    {
        RUNTIME_F_ERROR("checkConfig invalid! pjId[%ld], pcId[%ld], side[%d]: %s\n", project_id, protocol_id, static_cast<int32_t>(side), new_cfg_json.dump().c_str());

        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, 
            RuntimeError(RuntimeError::kInternalError),
            "protocol config check invalid");
    }
    
    std::function<bool(const nlohmann::json&)> persist_func;
    std::function<bool(const nlohmann::json&)> rollback_func;
    using RuntimeUpdateFunc = decltype(&ProjectServer::UpdateReqCfgProtocolItem);
    RuntimeUpdateFunc runtime_func = nullptr;

    std::optional<std::string> new_runtime_key;
    std::string old_runtime_key = access_info.runtime_key;

    if(ProtocolSide::kRequest == side)
    {
        if(!check_result.runtime_key.has_value())
        {
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeKeyInvalid, 
                RuntimeError(RuntimeError::kInternalError),
                check_result.message);
        }

        persist_func = [this, 
            ctx, 
            protocol_id, new_runtime_key = check_result.runtime_key.value()](const nlohmann::json& merge_cfg){

            return protocol_svc_->UpdateReqCfg(ctx, protocol_id, new_runtime_key, merge_cfg);
        };

        rollback_func = [this, 
            ctx, 
            protocol_id, 
            &old_runtime_key](const nlohmann::json& old_cfg_json){

            return protocol_svc_->UpdateReqCfg(ctx, protocol_id, old_runtime_key, old_cfg_json);
        };

        runtime_func = &ProjectServer::UpdateReqCfgProtocolItem;

    }
    else if(ProtocolSide::kResponse == side)
    {
        persist_func = [this, ctx, protocol_id](const nlohmann::json& merge_cfg){

            return protocol_svc_->UpdateRespCfg(ctx, protocol_id, merge_cfg);
        };

        rollback_func = [this, 
            ctx,
            protocol_id](const nlohmann::json& old_cfg){

            return protocol_svc_->UpdateRespCfg(ctx, protocol_id, old_cfg);
        };

        runtime_func = &ProjectServer::UpdateRespCfgProtocolItem;
    }


    if(!persist_func(new_cfg_json))
    {
        RUNTIME_F_ERROR("protocol update config error! pjId[%ld], pcId[%ld], side[%d]\n", project_id, protocol_id, static_cast<int32_t>(side));

        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError),
        "protocol update config error");
    }
    // DB 保存成功
    receipt.persisted = 1;
  

    // 注意: 运行态正在运行 且 协议项是上线状态
    if(ProjectRuntimeState::kRunning == access_info.project_runtime_state
        && ProtocolConfigState::kOn == access_info.protocol_config_state)
    {
        auto runtime_result = InvokeOnLoopSync(pj_server->getLoop(), 1000, [runtime_func,
            pj_server, 
            protocol_id,
            cfg = std::move(new_cfg_json)](){
      
            return std::invoke(runtime_func, pj_server.get(), protocol_id, cfg);

        });
        if(!runtime_result.ok())
        {
            if(!rollback_func(old_cfg_json))
            {
                RUNTIME_F_ERROR("UpdateBody rollback error! pjId[%ld], pcId[%ld], side[%d]\n", project_id,protocol_id, static_cast<int32_t>(side));

                return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeRollbackFailed, 
                    runtime_result.error, 
                    runtime_result.error.toMsg(), 
                    RuntimeMutationReceipt::PersistedOk());
            }
            else
            {
                receipt.persisted = 0;
            }
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
                runtime_result.error, 
                "protocol runtime apply failed",
                receipt);
        }
        receipt.runtime_applied = 1;
    }

    return ProtocolRuntimeResult::Success(receipt, ProtocolRuntimeSnapshot{
        .project_id = project_id,
        .protocol_id = protocol_id,
        .config_state = access_info.protocol_config_state
    });
}


ProtocolRuntimeResult ProjectRuntimeManager::updateProtocolBodyImpl(kit_muduo::HttpContextPtr ctx, int64_t project_id, int64_t protocol_id, ProtocolSide side, ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    ProtocolAccessInfo access_info;
    if(!protocol_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError),
        "get access info failed");
    }

    if(access_info.project_id != project_id
        || (ProtocolSide::kRequest != side && ProtocolSide::kResponse != side)
        || (body_type <= ProtocolBodyType::kNone || body_type >= ProtocolBodyType::kMax))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol project mismatch");
    }

    // body格式检查
    auto body_check = ProtocolBodyPipeline::CheckBody({
        .body_type = body_type,
        .body_data = body_data,
    });
    if(!body_check.ok)
    {
        RUNTIME_F_ERROR("protocol body check invalid! pcId[%ld], side[%d], type[%s]: \n",
            protocol_id,
            static_cast<int32_t>(side),
            ProtocolBodyTypeToString(body_type).c_str(),
            body_check.message.c_str());

        return ProtocolRuntimeResult::Failed(
            RuntimeControlCode::kInvalidArgument,
            RuntimeError(RuntimeError::kInternalError),
            body_check.message);
    }

    auto pj_server = findServer(project_id);

    auto result = CheckProtocolRuntimeCondition(access_info,  
        ProjectRuntimeState::kRunning == access_info.project_runtime_state, 
        pj_server == nullptr);
    if(!result.ok())
    {
        return result;
    }
    RuntimeMutationReceipt receipt;

    ProtocolBodyType old_body_type;
    std::vector<char> old_body_data;
    if(ProjectRuntimeState::kRunning == access_info.project_runtime_state
        && ProtocolConfigState::kOn == access_info.protocol_config_state)
    {
        if(!protocol_svc_->GetBodyInfoById(ctx, protocol_id, side, old_body_type, old_body_data))
        {
            RUNTIME_F_ERROR("GetBodyInfoById error! pcId[%ld], side[%d]\n", protocol_id, static_cast<int32_t>(side));

            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kProtocolNotFound, RuntimeError(RuntimeError::kInternalError),
            "protocol body info get error");
        }
    }

    if(!protocol_svc_->UpdateBody(ctx, protocol_id, side, body_type, body_data))
    {
        RUNTIME_F_ERROR("UpdateBody error! pcId[%ld], side[%d], body_type[%s]\n", protocol_id, static_cast<int32_t>(side), ProtocolBodyTypeToString(body_type).c_str());

        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError),
        "protocol body info update error");
    }
    // DB 保存成功
    receipt.persisted = 1;
  
    // 注意: 运行态正在运行 且 协议项是上线状态
    if(ProjectRuntimeState::kRunning == access_info.project_runtime_state
        && ProtocolConfigState::kOn == access_info.protocol_config_state)
    {
        auto runtime_result = InvokeOnLoopSync(pj_server->getLoop(), 1000, [pj_server, 
            protocol_id,
            side, 
            body_type, 
            mv_data = std::move(body_data)](){
      
            return pj_server->UpdateBodyProtocolItem(protocol_id, side, body_type, mv_data);
        });
        if(!runtime_result.ok())
        {
            if(!protocol_svc_->UpdateBody(ctx, protocol_id, side, old_body_type, old_body_data))
            {
                RUNTIME_F_ERROR("UpdateBody rollback error! pcId[%ld], side[%d]\n", protocol_id, static_cast<int32_t>(side));
                return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeRollbackFailed, 
                    runtime_result.error, 
                    runtime_result.error.toMsg(), 
                    RuntimeMutationReceipt::PersistedOk());
            }
            else
            {
                receipt.persisted = 0;
            }
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kRuntimeApplyFailed, 
                runtime_result.error, 
                "protocol runtime apply failed",
                receipt);
        }
        receipt.runtime_applied = 1;
    }

    return ProtocolRuntimeResult::Success(receipt, ProtocolRuntimeSnapshot{
        .project_id = project_id,
        .protocol_id = protocol_id,
        .config_state = access_info.protocol_config_state
    });
}

ProtocolRuntimeResult ProjectRuntimeManager::reconfigProtocolImpl(kit_muduo::HttpContextPtr ctx, Protocol &p)
{
    int64_t protocol_id = p.m_id;
    ProtocolAccessInfo access_info;
    if(!protocol_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInternalError, RuntimeError(RuntimeError::kInternalError),
        "get access info failed");
    }
    int64_t project_id = p.m_projectId;

    // 注意: 这个接口只有重配置协议可以走
    if(access_info.project_id != project_id
        || ProtocolConfigState::kReConfig != access_info.protocol_config_state
        || access_info.protocol_type != p.m_type)
    {
        return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
        "protocol project mismatch");
    }

    auto result = CheckProtocolRuntimeCondition(access_info, false, true);
    if(!result.ok())
    {
        return result;
    }

    // body格式检查
    auto body_check = ProtocolBodyPipeline::CheckFullProtocol(p);
    if(!body_check.ok)
    {
        RUNTIME_F_ERROR("protocol body check invalid: %s\n", body_check.message.c_str());
        return ProtocolRuntimeResult::Failed(
            RuntimeControlCode::kInvalidArgument,
            RuntimeError(RuntimeError::kInternalError),
            body_check.message);
    }


    std::shared_ptr<CustomTcpPattern> pattern = nullptr;

    // 注意: 需要上线且是TCP再做格式信息处理
    if(ProtocolType::kCustomTcp == access_info.protocol_type)
    {
        pattern = CustomTcpPatternFactory::Create(project_svc_->GetPatternInfoById(ctx, project_id));
        if(!pattern)
        {
            return ProtocolRuntimeResult::Failed(RuntimeControlCode::kInvalidArgument, RuntimeError(RuntimeError::kInternalError),
            "pattern info invalid");
        }

    }

    auto build_result = ProtocolConfigPipeline::buildItem({
        .full_config = ProtocolFullConfigSpec{
            .type = p.m_type,
            .req_cfg = p.m_reqCfg,
            .resp_cfg = p.m_respCfg,
        },
        .ori_protocol = p,
        .custom_tcp_pattern = pattern,
    });
    if(!build_result.ok || !build_result.runtime_key.has_value())
    {
        RUNTIME_F_ERROR("create protocol item error! pjId[%ld], type[%d], name[%s]: %s\n", p.m_projectId, static_cast<int32_t>(p.m_type), p.m_name.c_str(), build_result.message.c_str());

       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kCreateProtocolItemFailed, 
        RuntimeError(RuntimeError::kInternalError),
        build_result.message);
    }
    // 运行键更新一下
    p.m_runtimeKey = build_result.runtime_key.value();
    auto item = build_result.item;

    // 注意: 暗含1个状态转移 kReconfig--->kOff
    p.m_status = access_info.protocol_status;
    p.m_type = access_info.protocol_type;
    p.m_configState = ProtocolConfigState::kOff;
    if(!protocol_svc_->UpdateById(ctx, p))
    {
        RUNTIME_F_ERROR("protocol update error! pjId[%ld], type[%d], name[%s]\n", p.m_projectId, static_cast<int32_t>(p.m_type), p.m_name.c_str());

       return ProtocolRuntimeResult::Failed(RuntimeControlCode::kPersistFailed, RuntimeError(RuntimeError::kInternalError),
        "protocol update db error");
    }

    return ProtocolRuntimeResult::Success(RuntimeMutationReceipt::PersistedOk(), ProtocolRuntimeSnapshot{
        .project_id = p.m_projectId,
        .protocol_id = protocol_id,
        .config_state = p.m_configState
    });
}


}
