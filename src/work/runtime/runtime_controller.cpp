/**
 * @file runtime_controller.cpp
 * @brief 运行态业务管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-08 15:20:24
 * @copyright Copyright (c) 2026 Kewin Li
 */

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

#include <mutex>
#include <utility>
#include <unistd.h>

using namespace kit_muduo;

namespace kit_domain {

    ProjectRuntimeManager::ProjectRuntimeManager(std::shared_ptr<ProjectSvcInterface> project_svc,
        std::shared_ptr<ProtocolSvcInterface> protocol_svc,
        size_t runtime_loop_capacity)
    :project_svc_(std::move(project_svc))
    ,protocol_svc_(std::move(protocol_svc))
    ,loop_pool_(runtime_loop_capacity)
{


}




ProjectRuntimeResult ProjectRuntimeManager::startProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return submitProjectOperation(project_id,
        RuntimeOperationKind::kStartProject,
        "startProject",
        RuntimeOperationOptions{3000, true},
        [this, ctx, project_id](){
            return startProjectImpl(ctx, project_id);
        });
}

ProjectRuntimeResult ProjectRuntimeManager::stopProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return submitProjectOperation(project_id,
        RuntimeOperationKind::kStopProject,
        "stopProject",
        RuntimeOperationOptions{3000, true},
        [this, ctx, project_id](){
            return stopProjectImpl(ctx, project_id);
        });
}

ProjectRuntimeResult ProjectRuntimeManager::delProject(kit_muduo::HttpContextPtr ctx, int64_t project_id)
{
    return submitProjectOperation(project_id,
        RuntimeOperationKind::kDeleteProject,
        "delProject",
        RuntimeOperationOptions{3000, true},
        [this, ctx, project_id](){
            return delProjectImpl(ctx, project_id);
        });
}

RuntimeRecoverResult ProjectRuntimeManager::recover(kit_muduo::HttpContextPtr ctx)
{
    RuntimeRecoverResult result;
    auto active_pjs = project_svc_->GetAllActive(ctx);

    std::vector<int64_t> failed_pj_ids;
    for(auto &p : active_pjs)
    {
        const int64_t project_id = p.m_id;
        auto item_result = submitProjectOperation(project_id,
            RuntimeOperationKind::kRecoverProject,
            "recoverProject",
            RuntimeOperationOptions{3000, true},
            [this, ctx, project = std::move(p)](){
                return recoverProjectImpl(ctx, project);
            });
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
    return submitProjectOperation(project_id,
        RuntimeOperationKind::kEditPatternInfo,
        "editPatternInfo", RuntimeOperationOptions{3000, true}, [this, ctx, project_id, pattern_info](){
            return editPatternInfoImpl(ctx, project_id,pattern_info);
        });
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
    assert(runtime_projects_.erase(project_id) == 1);

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


    auto runtime_enabled_pcs_result = registerRuntimeEnabledProtocolsLocked(ctx, project_server, project_id);
    if(!runtime_enabled_pcs_result.ok())
    {
        return ProjectRuntimeResult::Failed(RuntimeControlCode::kCreateProtocolItemFailed, std::move(runtime_enabled_pcs_result.error));
    }

    uint16_t cur_listen_port = project_server->getBindAddr().toPort();

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
        int64_t project_id)
{
    RuntimeResult<void> result;
    // 查询当前测试服务上所有`未软删且上线状态`的协议项
    // 当前策略：只要失败一项就报错返回
    std::vector<Protocol> active_pcs = protocol_svc_->GetActiveByProject(ctx, project_id);
    for(auto &pc : active_pcs)
    {
        auto protocol_item = ProtocolItemFactory::Create(std::make_shared<Protocol>(pc), project_server);

        result = InvokeOnLoopSync(project_server->getLoop(), 3000, [project_server,
            protocol_item](){
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

}
