/**
 * @file interaction.h
 * @brief 全局协议交互详情发布器 初始化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-20 17:19:06
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_IOC_INTERACTION_H__
#define __KIT_IOC_INTERACTION_H__

#include "domain/protocol_interaction_publisher.h"

#include <memory>
#include <vector>

namespace kit_app {


std::shared_ptr<kit_domain::ProtocolInteractionPublisher> InitInteractionPublisher(const std::vector<std::shared_ptr<kit_domain::InteractionSink>> &hubs);

}


#endif //__KIT_IOC_INTERACTION_H__