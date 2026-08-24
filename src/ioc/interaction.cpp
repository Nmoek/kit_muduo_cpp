/**
 * @file interaction.cpp
 * @brief 全局协议交互详情发布器 初始化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-20 17:13:47
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "ioc/interaction.h"
#include "app_config.h"

using namespace kit_domain;

namespace kit_app {

namespace {

ProtocolInteractionPublisherConfig MakePcInteracPublisherConfig()
{
    ProtocolInteractionPublisherConfig config;
#define XX(VAR) \
    config.VAR = *    (APP_CONFIG_VARS_WORK_INTRAC(VAR)->value())

    XX(queue_capacity);
    XX(stop_drain_timeout_ms);
#undef XX

#define XX(VAR) \
    config.capture_options.VAR = *    (APP_CONFIG_VARS_WORK_INTRAC(VAR)->value())

    XX(capture_max_text_bytes);
    XX(capture_max_hex_bytes);
    XX(capture_max_binary_attachment_bytes);

#undef XX
    return config;
}


} // namespace

std::shared_ptr<ProtocolInteractionPublisher> InitInteractionPublisher(const std::vector<std::shared_ptr<InteractionSink>> &hubs)
{
    const auto publisher_config = MakePcInteracPublisherConfig();


    auto publisher = std::make_shared<ProtocolInteractionPublisher>(
        std::move(hubs)
        ,std::move(publisher_config)
    );

    return publisher;
}





}