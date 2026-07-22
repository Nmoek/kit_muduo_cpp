/**
 * @file project_server.cpp
 * @brief  测试服务实际服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 15:43:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/project_server.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_observation.h"
#include "domain/runtime_loop_pool.h"
#include "domain/domain_log.h"
#include "domain/protocol_interaction_hub.h"
#include "net/tcp_server.h"
#include "net/tcp_server.h"

#include <chrono>
#include <future>
#include <memory>


using namespace kit_muduo;

namespace kit_domain {

namespace {

inline static EventLoop* CheckLoop(EventLoop *loop)
{
    if(!loop)
    {
        throw std::invalid_argument("loop* is null");
    }
    return loop;
}

}

ProjectServer::ProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop, const kit_muduo::InetAddress &addr, const std::string &name)
    :tcp_server_(
        CheckLoop(lease_loop->loop()), 
        addr, 
        name, 
        kit_muduo::TcpServer::KReusePort
    )
    ,project_id_(project_id)
    ,lease_loop_(lease_loop)
    ,notice_cache_(std::make_shared<InteractionRecordCache>(InteractionRecordCacheKey{
        .scope = InteractionScope::kProject,
        .project_id = project_id,
        .protocol_id = 0, //注意 这里必须为0
    }))
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


void ProjectServer::start() 
{
    tcp_server_.setThreadNum(0); // 使用单线程模式
    tcp_server_.start();
}

bool ProjectServer::stop()
{
    bool expected = false;
    if(!stopped_.compare_exchange_strong(expected, true))
    {
        return true;
    }

    bool ok = WaitRuntimeStopDone("CustomTcpProjectServer", project_id_, [this](std::function<void()> done){
        tcp_server_.stopAsync(std::move(done));
    });

    if(!ok)
    {
        stopped_ = false;
        return false;
    }
    if(notice_cache_)
    {
        notice_cache_->close();
    }
    closeAllProtocolInteractionCaches();

    lease_loop_->release();
    return true;
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