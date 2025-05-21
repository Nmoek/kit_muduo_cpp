/**
 * @file default_poller.cpp
 * @brief 生成默认Poller对象
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 16:16:14
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/poller.h"
#include "net/event_loop.h"
#include "net/poll_poller.h"
#include "net/epoll_poller.h"

#include <stdlib.h>

namespace kit_muduo {


Poller* Poller::NewDefaultPoller(EventLoop *loop)
{
    if(::getenv("KIT_MUDUO_POLLER_POLL"))
    {
        return new PollPoller(loop);
        ;
    }
    else
    {
        return new EpollPoller(loop);
    }
}


}   // kit_muduo