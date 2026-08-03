/**
 * @file protocol_interaction_publisher.cpp
 * @brief 协议项交互数据发布器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-09 16:49:23
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/thread.h"
#include "domain/domain_log.h"
#include "domain/protocol_interaction_observation.h"
#include "domain/protocol_interaction_publisher.h"
#include "base/time_stamp.h"


#include <atomic>
#include <exception>

using namespace kit_muduo;

namespace kit_domain {


ProtocolInteractionPublisher::ProtocolInteractionPublisher(std::vector<std::shared_ptr<InteractionSink>> sinks, ProtocolInteractionPublisherConfig config)
    :sinks_(std::move(sinks))
    ,config_(std::move(config))
    ,queue_(config_.queue_capacity)
    ,worker_thread_(nullptr)
    ,is_stopping_(false)

{

}

ProtocolInteractionPublisher::~ProtocolInteractionPublisher()
{
    stop();
}

void ProtocolInteractionPublisher::start()
{
    if(is_stopping_.load(std::memory_order_acquire))
    {
        return;
    }


    worker_thread_ = std::make_unique<Thread>([this](){
        workLoop();
    });
    worker_thread_->start();

}

void ProtocolInteractionPublisher::stop()
{
    bool expect = false;
    if(!is_stopping_.compare_exchange_strong(expect, true, std::memory_order_acq_rel, std::memory_order_acq_rel))
    {
        return;
    }
    worker_cond_.notify_one();

    if(worker_thread_)
    {
        worker_thread_->join();
    }
}

void ProtocolInteractionPublisher::publish(ProtocolInteractionObservation obs)
{
    if(is_stopping_)
    {
        PUBLISHER_F_ERROR("protocol interaction publisher stopping...\n");
        return;
    }

    auto queue_obs = std::make_shared<QueueObservation>();
    queue_obs->obs = std::move(obs);

    if(!queue_.tryPush(queue_obs))
    {
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        PJSERVER_F_WARN("interaction publish queue full, drop observation!! pjId[%ld], pcId[%ld], peer[%s], time[%ld]\n", queue_obs->obs.project_id, 
            queue_obs->obs.protocol_id,
            queue_obs->obs.peer_addr.c_str(),
            queue_obs->obs.time_ms);
        return;
    }

    worker_cond_.notify_one();
}



void ProtocolInteractionPublisher::fillInteractionSide(InteractionRecord &record,
    InteractionSide& dst,
    const InteractionSideCapture &src,
    ProtocolSide side)
{
    dst.meta = std::move(src.meta);
    dst.head_text = std::move(src.head_text);

    InteractionPayloadHint hint;
    hint.protocol_type = record.protocol_type;
    hint.expect_body_type = src.expect_body_type;
    hint.content_meta = std::move(src.content_meta);
    hint.prefer_hex_text_for_binary = src.prefer_hex_text_for_binary;

    dst.body = InteractionBody::BuildFromBytes(src.body_bytes, 
        hint, 
        side, 
        config_.capture_options, 
        record.binary_sidecars);
    
    if(src.hasRaw())
    {
        dst.raw_packet = InteractionRawPacket::BuildRawPacketFromBytes(src.raw_bytes, 
            side, 
            config_.capture_options, 
            record.binary_sidecars);
    }

}


void ProtocolInteractionPublisher::workLoop()
{
    while(!is_stopping_.load(std::memory_order_acquire))
    {
        std::shared_ptr<QueueObservation> queue_obs = nullptr;
        while(queue_.tryPop(queue_obs))
        {
            queueObsHandle(queue_obs);
        }

        std::unique_lock<std::mutex> lock(worker_mtx_);
        worker_cond_.wait(lock, [this](){
            return is_stopping_.load(std::memory_order_acquire) || !queue_.empty();
        });
    }

    // 带超时回收逻辑
    drainQueueTimeOut(kit_muduo::TimeStamp::NowMs() + config_.stop_drain_timeout_ms);

}

void ProtocolInteractionPublisher::drainQueueTimeOut(int64_t will_timeout)
{
    while(!queue_.empty() && kit_muduo::TimeStamp::NowMs() < will_timeout)
    {
        std::shared_ptr<QueueObservation> queue_obs = nullptr;
        while(queue_.tryPop(queue_obs))
        {
            if(kit_muduo::TimeStamp::NowMs() >= will_timeout)
            {
                PUBLISHER_F_DEBUG("interaction queue drain timeout!\n");
                return;
            }
            queueObsHandle(queue_obs);
        }
    }

    if(!queue_.empty())
    {
        PUBLISHER_F_DEBUG("interaction queue not empty\n");
    }
}


InteractionRecord ProtocolInteractionPublisher::buildRecord(const QueueObservation &queue_obs)
{
    ProtocolInteractionObservation obs = std::move(queue_obs.obs);

    if (obs.scope != InteractionScope::kProtocol
        && obs.scope != InteractionScope::kProject)
    {
        throw std::invalid_argument("unknown interaction scope");
    }
    if (obs.project_id <= 0)
    {
        throw std::invalid_argument("invalid interaction project_id");
    }
    if (obs.scope == InteractionScope::kProtocol && obs.protocol_id <= 0)
    {
        throw std::invalid_argument("invalid protocol interaction protocol_id");
    }

    InteractionRecord record;
    record.scope = obs.scope;
    record.project_id = obs.project_id;
    record.protocol_id = (InteractionScope::kProject == record.scope ? 0 : obs.protocol_id);
    record.cache_instance_id = obs.cache_instance_id;
    record.protocol_type = obs.protocol_type;
    record.time_ms = obs.time_ms > 0 ? obs.time_ms : TimeStamp::NowMs();
    record.peer_addr = std::move(obs.peer_addr);
    record.result = obs.result;
    record.error_message = std::move(obs.error_message);

    fillInteractionSide(record, record.request, obs.request, ProtocolSide::kRequest);

    fillInteractionSide(record, record.response, obs.response, ProtocolSide::kResponse);
    return record;
}


void ProtocolInteractionPublisher::publishRecord(InteractionRecord record)
{
    if(sinks_.empty())
    {
        PUBLISHER_F_WARN("interaction publisher has no sink\n");
        return;
    }

    for(size_t i = 0; i < sinks_.size(); ++i)
    {
        try
        {
            if(sinks_[i])
            {
                sinks_[i]->publish(record);
            }
            
        } catch(const std::exception &e) {
            PUBLISHER_F_ERROR("interaction sink publish failed: %s\n", e.what());
        } catch(...) {
            PUBLISHER_F_ERROR("interaction sink publish unknown exception\n");
        }
    }
}

void ProtocolInteractionPublisher::queueObsHandle(std::shared_ptr<QueueObservation>& queue_obs)
{
    if(!queue_obs)
    {
        PUBLISHER_F_WARN("interaction queue data null\n");
        return;
    }
    try {

        auto cache = queue_obs->obs.weak_record_cache.lock();
        if(!cache || !cache->isActive())
        {
            PUBLISHER_F_INFO("interaction cache not active\n");
            return;
        }

        // record构造
        auto record = buildRecord(*queue_obs);

        // record缓存
        if(!cache->tryAppend(record))
        {
            return;
        }
        
        // record推送
        publishRecord(std::move(record));

        processed_count_.fetch_add(1, std::memory_order_relaxed);

    } catch(const std::exception &e) {
        failed_count_.fetch_add(1, std::memory_order_relaxed);
        PUBLISHER_F_ERROR("interaction publish failed: %s\n", e.what());
    } catch(...) {
        failed_count_.fetch_add(1, std::memory_order_relaxed);
        PUBLISHER_F_ERROR("interaction publish unknown exception\n");
    }
}


}