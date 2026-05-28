/**
 * @file application.cpp
 * @brief 应用入口
 * @author ljk5
 * @version 1.0
 * @date 2025-09-19 11:55:56
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "app_log.h"
#include "application.h"

#include "domain/runtime_result.h"
#include "net/tcp_connection.h"
#include "service/svc_project.h"
#include "service/svc_protocol.h"
#include "domain/type.h"
#include "domain/project_server.h"
#include "domain/project_server_factory.h"
#include "domain/protocol_item.h"
#include "domain/protocol.h"

#include <memory>
#include <mutex>
#include <semaphore.h>
#include <stdexcept>
#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_app {

Application::Application(std::shared_ptr<kit_muduo::http::HttpServer> server)
    :server_(server)
{

}


void Application::addServer(int64_t projectId, std::shared_ptr<kit_domain::ProjectServer> server)
{
    std::unique_lock<std::mutex> lock(mtx_);
    project_servers_[projectId] = server;
}

std::shared_ptr<kit_domain::ProjectServer> Application::findServer(int64_t projectId)
{
    std::unique_lock<std::mutex> lock(mtx_);
    auto it = project_servers_.find(projectId);
    return it == project_servers_.end() ? nullptr : it->second;

}

void Application::delServer(int64_t projectId)
{
    std::unique_lock<std::mutex> lock(mtx_);
    auto n =project_servers_.erase(projectId);
    assert(n == 1);
}

std::shared_ptr<kit_domain::RuntimeLease> Application::leaseLoop(kit_domain::ProjectRuntimeUid uid)
{
    int32_t try_count = 3;
    RuntimeResult<std::shared_ptr<kit_domain::RuntimeLease>> result;
    while(try_count--)
    {
        result = loop_pool_.acquire(uid);
        if(!result.ok())
        {
            if(RuntimeError::kRuntimeLoopPoolExhausted == result.error.toInt())
            {
                usleep(50000);
                continue;
            }
            APP_F_ERROR("runtime loop lease falid: %s\n", result.error.toMsg().c_str());
            break;
        }
        else
        {
            break;
        }
    }
    if(try_count <= 0)
    {
        APP_F_WARN("runtime loop lease timeout: %s\n", result.error.toMsg().c_str());
    }
    return result.val;
}



bool Application::recover(std::shared_ptr<ProjectSvcInterface> project_svc, std::shared_ptr<ProtocolSvcInterface> protocol_svc)
{
    if(!project_svc || !protocol_svc)
    {
        return false;
    }


    /*注意: 不要直接操作DAO层 操作service层 */
    std::vector<int64_t> faild_ids;
    bool ok = false;
    auto pjs = project_svc->GetAllActive(nullptr);

    for(auto &pj : pjs)
    {
        auto lease_loop = leaseLoop(pj.m_id);
        if(!lease_loop)
        {
            APP_F_ERROR("runtime loop lease faild!\n");
            throw std::runtime_error("runtime loop lease faild");
        }

        // 使用工厂模式创建ProjectServer
        auto project_server = ProjectServerFactory::Create(pj, lease_loop);
        if (!project_server) 
        {
            APP_F_ERROR("Failed to create ProjectServer for project_id[%d], protocol_type[%d]\n",  pj.m_id, static_cast<int32_t>(pj.m_protocolType));

            //记录开启失败id
            faild_ids.push_back(pj.m_id);
            continue;
        }

        this->addServer(pj.m_id, project_server);
        
        // 为服务器添加协议  暂时同步添加 体量大之后再考虑异步同时启动
        /* 操作Service层接口 */
        std::vector<Protocol> pcs = protocol_svc->GetActiveByProject(nullptr, pj.m_id);
        for(auto &pc : pcs)
        {
            auto protocol_item = ProtocolItemFactory::Create(std::make_shared<Protocol>(pc), project_server);
            if(!protocol_item)
            {
                ok = protocol_svc->Del(nullptr, pc.m_id);
                if(!ok)
                {
                    APP_F_ERROR("ProtocolItem re-Del faild!  protocol_id[%d] protocol_type[%d] project_id[%d] \n",  pc.m_id, static_cast<int32_t>(pc.m_type), pc.m_projectId);
                }
                APP_F_ERROR("create ProtocolItem faild!  protocol_id[%d] protocol_type[%d] project_id[%d] \n",  pc.m_id, static_cast<int32_t>(pc.m_type), pc.m_projectId);
                continue;
            }

            project_server->AddProtocolItem(protocol_item);
            
            APP_F_DEBUG("[%d][%s][%d] add success!\n", pc.m_id, pc.m_name.c_str(), pc.m_projectId);
        }
        uint16_t cur_listen_port = project_server->getBindAddr().toPort();
        // 现行端口号回写
        ok = project_svc->UpdateRuntimeStatus(nullptr, pj.m_id, ProjectStatus::ON_STATUS, cur_listen_port);
        if(!ok)
        {
            APP_F_WARN("service update status failed! pjId[%d] name[%s] listen_port[%u]\n", pj.m_id, pj.m_name.c_str(), cur_listen_port);
        }

        project_server->start();
    }

    // 处理失败的测试服务
    for(auto &pj_id : faild_ids)
    {
        ok = project_svc->UpdateRuntimeStatus(nullptr, pj_id, ProjectStatus::OFF_STATUS, 0);
        if(!ok)
        {
            APP_F_WARN("service update status failed! pjId[%ld] \n", pj_id);
            continue;
        }
    }
    return true;
}


void Application::shutdown()
{
    if(server_)
    {
        server_->stop();
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        for(auto &it : project_servers_)
        {
            if(it.second)
            {
                it.second->stop();
            }
        }
        project_servers_.clear();
    }

    loop_pool_.shutdown();
}


}
