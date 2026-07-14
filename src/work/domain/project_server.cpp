/**
 * @file project_server.cpp
 * @brief  测试服务实际服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 15:43:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/project_server.h"
#include "base/time_stamp.h"
#include "base/util.h"
#include "domain/protocol_interaction.h"
#include "domain/runtime_loop_pool.h"
#include "domain/domain_log.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/type.h"
#include "web/web_common.h"

#include <chrono>
#include <future>


using namespace kit_muduo;

namespace kit_domain {

ProjectServer::ProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop)
    :project_id_(project_id)
    ,lease_loop_(lease_loop)
{ 

}

bool ProjectServer::isActive() const 
{ 
    return !lease_loop_->isRelease(); // 未归还说明还活跃
}

kit_muduo::EventLoop *ProjectServer::getLoop() 
{ 
    return lease_loop_->loop();
}

bool WaitRuntimeStopDone(const char *name,
    int64_t project_id,
    const std::function<void(std::function<void()>)> &start_stop)
{
    auto p = std::make_shared<std::promise<void>>();
    auto f = p->get_future();

    start_stop([p](){
        p->set_value();
    });

    auto status = f.wait_for(std::chrono::seconds(5));
    if(std::future_status::ready != status)
    {
        PJSERVER_F_ERROR("%s stop timeout! pjId[%ld]\n", name, project_id);
        return false;
    }


    return true;

}


void ProjectServer::emitObserve(ProtocolInteractionObservation obs)
{

    try {

        if(observe_cb_)
        {
            observe_cb_(std::move(obs));
        }
        else
        {
            PJSERVER_F_WARN("project observe callback function null \n");
        }

    } catch(const std::exception &e) {
        PJSERVER_F_ERROR("project observe callback exception: %s\n", e.what());

    } catch(...) {
        PJSERVER_F_ERROR("project observe callback unknown exception\n");
    }
}



}