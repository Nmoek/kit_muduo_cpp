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

#include "net/http/http_request.h"
#include "net/http/http_context.h"
#include "net/http/http_response.h"
#include "net/http/http_servlet.h"
#include "net/http/http_util.h"
#include "net/event_loop.h"
#include "base/content_parser.h"
#include "net/tcp_connection.h"
#include "base/thread.h"
#include "base/event_loop_thread.h"
#include "service/svc_project.h"
#include "service/svc_protocol.h"
#include "domain/type.h"
#include "domain/project_server.h"
#include "domain/project_server_factory.h"
#include "domain/protocol_item.h"
#include "domain/protocol.h"

#include <memory>
#include <semaphore.h>
#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_app {

Application::Application(std::shared_ptr<kit_muduo::http::HttpServer> server)
    :_server(server)
{}


void Application::AddServer(int64_t projectId, std::shared_ptr<kit_domain::ProjectServer> server)
{
    std::unique_lock<std::mutex> lock(_mtx);
    _projectServers[projectId] = server;
}

std::shared_ptr<kit_domain::ProjectServer> Application::FindServer(int64_t projectId)
{
    std::unique_lock<std::mutex> lock(_mtx);
    auto it = _projectServers.find(projectId);
    return it == _projectServers.end() ? nullptr : it->second;

}


bool Application::Recover(std::shared_ptr<ProjectSvcInterface> project_svc, std::shared_ptr<ProtocolSvcInterface> protocol_svc)
{
    if(!project_svc || !protocol_svc)
    {
        return false;
    }

    

    /*注意: 不要直接操作DAO层 操作service层 */
    auto pjs = project_svc->GetAllActive(nullptr);

    for(auto &pj : pjs)
    {
        // 使用工厂模式创建ProjectServer
        auto project_server = ProjectServerFactory::Create(pj);
        if (!project_server) 
        {
            RECOVER_F_ERROR("Failed to create ProjectServer for project_id[%d], protocol_type[%d]\n",  pj.m_id, static_cast<int32_t>(pj.m_protocolType));
            continue;
        }

        this->AddServer(pj.m_id, project_server);

        // 为服务器添加协议  暂时同步添加 体量大之后再考虑异步同时启动
        /* 操作Service层接口 */
        std::vector<Protocol> pcs = protocol_svc->GetActiveByProject(nullptr, pj.m_id);
        for(auto &pc : pcs)
        {
            auto protocol_item = ProtocolItemFactory::Create(std::make_shared<Protocol>(pc), project_server);
            if(!protocol_item)
            {
                RECOVER_F_ERROR("create ProtocolItem faild!  protocol_id[%d] protocol_type[%d] project_id[%d] \n",  pc.m_id, static_cast<int32_t>(pc.m_type), pc.m_projectId);
                continue;
            }

            project_server->AddProtocolItem(protocol_item);
            
            RECOVER_F_DEBUG("[%d][%s][%d] add success!\n", pc.m_id, pc.m_name.c_str(), pc.m_projectId);
        }
      
    }
    return true;
}

}
