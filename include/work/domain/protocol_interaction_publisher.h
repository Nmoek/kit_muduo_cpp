/**
 * @file protocol_interaction_publisher.h
 * @brief 协议项交互数据发布器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-09 01:12:23
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __PROTOCOL_INTERACTION_PUBLISHER_H__
#define __PROTOCOL_INTERACTION_PUBLISHER_H__ 


#include "base/bounded_lock_free_queue.h"
#include "base/noncopyable.h"
#include "base/thread.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/protocol_interaction_observation.h"
#include <atomic>
#include <condition_variable>
#include <initializer_list>

namespace kit_domain {

struct ProtocolInteractionPublisherConfig
{
    size_t queue_capacity{4*1024};
    int64_t stop_drain_timeout_ms{2000}; // 单位 ms
    InteractionCaptureOptions capture_options{};
};

class ProtocolInteractionPublisher : kit_muduo::Noncopyable
{
public:
    explicit ProtocolInteractionPublisher(std::vector<std::shared_ptr<InteractionSink>> sinks, ProtocolInteractionPublisherConfig config = {});

    ~ProtocolInteractionPublisher();

    ProtocolInteractionPublisher(ProtocolInteractionPublisher&&) = delete;

    void start();
    void stop();
    const ProtocolInteractionPublisherConfig& config() const { return config_; }

    void publish(ProtocolInteractionObservation obs);

    // void addSink(std::shared_ptr<ProtocolInteractionSink> sink);

    /*******TODO  暂时不用*******/
    void clearProtocolInteraction(int64_t project_id, int64_t protocol_id);
    void clearProjectInteraction(int64_t project_id);
    /*******TODO  暂时不用*******/

private:
    struct QueueObservation
    {
        ProtocolInteractionObservation obs;
    };

    void workLoop();

    void drainQueueTimeOut(int64_t will_timeout);

    void queueObsHandle(std::shared_ptr<QueueObservation>& queue_obs);

    InteractionRecord buildRecord(const QueueObservation &queue_obs);

    void publishRecord(InteractionRecord record);

    void fillInteractionSide(InteractionRecord &record,
        InteractionSide& dst,
        const InteractionSideCapture &src,
        ProtocolSide side);


private:
    /// @brief 发布渠道列表
    std::vector<std::shared_ptr<InteractionSink>> sinks_;
    /// @brief 发布器配置
    ProtocolInteractionPublisherConfig config_{};

    /// @brief 待发布数据无锁队列 MPSC
    kit_muduo::BoundedLockFreeQueue<std::shared_ptr<QueueObservation>> queue_;
    
    /// @brief 发布器内部工作线程
    std::mutex worker_mtx_;
    std::condition_variable worker_cond_;
    std::unique_ptr<kit_muduo::Thread> worker_thread_;
    std::atomic_bool is_stopping_{false};

    std::atomic_uint64_t dropped_count_{0};
    std::atomic_uint64_t processed_count_{0};
    std::atomic_uint64_t failed_count_{0};


};



}
#endif // __PROTOCOL_INTERACTION_PUBLISHER_H__