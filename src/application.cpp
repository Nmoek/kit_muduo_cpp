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
#include "runtime/runtime_controller.h"

#include <memory>
#include <mutex>
#include <semaphore.h>
#include <stdexcept>
#include <unistd.h>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;

namespace kit_app {

Application::Application(std::shared_ptr<kit_muduo::http::HttpServer> server, std::shared_ptr<kit_domain::RuntimeControllerInterface> project_runtime_manager)
    :server_(server)
    ,project_runtime_manager_(std::move(project_runtime_manager))
{

}


void Application::addServer(int64_t project_id, std::shared_ptr<kit_domain::ProjectServer> server)
{
    project_runtime_manager_->addServer(project_id, server);
}

std::shared_ptr<kit_domain::ProjectServer> Application::findServer(int64_t project_id)
{
    return project_runtime_manager_->findServer(project_id);
}

void Application::delServer(int64_t project_id)
{
    return project_runtime_manager_->removeServer(project_id);
}



bool Application::recover()
{

    auto result = project_runtime_manager_->recover();
    if(!result.ok())
    {
        APP_F_INFO("recover info: code[%d], failed_count[%d], recovered_count[%d]\n", static_cast<int32_t>(result.status.code), 
            result.failed_count,
            result.recovered_count);
        for(auto & rp: result.runtime_projects)
        {
            APP_F_INFO("recover projects: pjId[%ld], code[%d], runtime_state[%d],listen_port[%u] \n", static_cast<long>(rp.snapshot.project_id),
                static_cast<int32_t>(rp.status.code), 
                static_cast<int32_t>(rp.snapshot.runtime_state),
                rp.snapshot.listen_port);
        }
    }

    return result.ok();
}


void Application::shutdown()
{
    if(server_)
    {
        server_->stop();
    }

    if(project_runtime_manager_)
    {
        project_runtime_manager_->shutdown();
    }
}


}
