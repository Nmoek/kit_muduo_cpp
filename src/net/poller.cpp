/**
 * @file poller.cpp
 * @brief IO复用组件类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 16:08:15
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/poller.h"
#include "net/channel.h"

namespace kit_muduo {

Poller::Poller(EventLoop *loop)
    :_ownerLoop(loop)
{

}

bool Poller::hasChannel(Channel *channel)
{
    auto it = _channels.find(channel->fd());
    return it != _channels.end() && it->second == channel;
}


}   //kit_muduo