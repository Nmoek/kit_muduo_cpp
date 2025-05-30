/**
 * @file net_log.h
 * @brief net模块日志定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 14:45:34
 * @copyright Copyright (c) 2025 Kewin Li
 */

 #include "base/log.h"

static auto g_net_logger = KIT_LOGGER("net");


/*********流式输出**********/
#define NET_DEBUG(module) \
    KIT_DEBUG(g_net_logger, module)
#define NET_INFO(module) \
    KIT_INFO(g_net_logger, module)
#define NET_WARN(module) \
    KIT_WARN(g_net_logger, module)
#define NET_ERROR(module) \
    KIT_ERROR(g_net_logger, module)
#define NET_FATAL(module) \
    KIT_FATAL(g_net_logger, module)

/**********变参输出***********/
#define NET_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(g_net_logger, module, fmt, ##__VA_ARGS__)
#define NET_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(g_net_logger, module, fmt, ##__VA_ARGS__)
#define NET_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(g_net_logger, module, fmt, ##__VA_ARGS__)
#define NET_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(g_net_logger, module, fmt, ##__VA_ARGS__)
#define NET_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(g_net_logger, module, fmt, ##__VA_ARGS__)

/*******channel模块*********/
#define CHANNEL_DEBUG()     NET_DEBUG("channel")
#define CHANNEL_INFO()      NET_INFO("channel")
#define CHANNEL_WARN()      NET_WARN("channel")
#define CHANNEL_ERROR()     NET_ERROR("channel")
#define CHANNEL_FATAL()     NET_FATAL("channel")

#define CHANNEL_F_DEBUG(fmt, ...)     NET_F_DEBUG("channel", fmt, ##__VA_ARGS__)
#define CHANNEL_F_INFO(fmt, ...)      NET_F_INFO("channel", fmt, ##__VA_ARGS__)
#define CHANNEL_F_WARN(fmt, ...)      NET_F_WARN("channel", fmt, ##__VA_ARGS__)
#define CHANNEL_F_ERROR(fmt, ...)     NET_F_ERROR("channel", fmt, ##__VA_ARGS__)
#define CHANNEL_F_FATAL(fmt, ...)     NET_F_FATAL("channel", fmt, ##__VA_ARGS__)
/*******channel模块*********/

/*******poller模块*********/
#define POLLER_DEBUG()     NET_DEBUG("poller")
#define POLLER_INFO()      NET_INFO("poller")
#define POLLER_WARN()      NET_WARN("poller")
#define POLLER_ERROR()     NET_ERROR("poller")
#define POLLER_FATAL()     NET_FATAL("poller")

#define POLLER_F_DEBUG(fmt, ...)     NET_F_DEBUG("poller", fmt, ##__VA_ARGS__)
#define POLLER_F_INFO(fmt, ...)      NET_F_INFO("poller", fmt, ##__VA_ARGS__)
#define POLLER_F_WARN(fmt, ...)      NET_F_WARN("poller", fmt, ##__VA_ARGS__)
#define POLLER_F_ERROR(fmt, ...)     NET_F_ERROR("poller", fmt, ##__VA_ARGS__)
#define POLLER_F_FATAL(fmt, ...)     NET_F_FATAL("poller", fmt, ##__VA_ARGS__)
/*******poller模块*********/

/*******EventLoop模块*********/
#define LOOP_DEBUG()     NET_DEBUG("event_loop")
#define LOOP_INFO()      NET_INFO("event_loop")
#define LOOP_WARN()      NET_WARN("event_loop")
#define LOOP_ERROR()     NET_ERROR("event_loop")
#define LOOP_FATAL()     NET_FATAL("event_loop")

#define LOOP_F_DEBUG(fmt, ...)     NET_F_DEBUG("event_loop", fmt, ##__VA_ARGS__)
#define LOOP_F_INFO(fmt, ...)      NET_F_INFO("event_loop", fmt, ##__VA_ARGS__)
#define LOOP_F_WARN(fmt, ...)      NET_F_WARN("event_loop", fmt, ##__VA_ARGS__)
#define LOOP_F_ERROR(fmt, ...)     NET_F_ERROR("event_loop", fmt, ##__VA_ARGS__)
#define LOOP_F_FATAL(fmt, ...)     NET_F_FATAL("event_loop", fmt, ##__VA_ARGS__)
/*******EventLoop模块*********/

/*******Socket模块*********/
#define SOCK_DEBUG()     NET_DEBUG("socket")
#define SOCK_INFO()      NET_INFO("socket")
#define SOCK_WARN()      NET_WARN("socket")
#define SOCK_ERROR()     NET_ERROR("socket")
#define SOCK_FATAL()     NET_FATAL("socket")

#define SOCK_F_DEBUG(fmt, ...)     NET_F_DEBUG("socket", fmt, ##__VA_ARGS__)
#define SOCK_F_INFO(fmt, ...)      NET_F_INFO("socket", fmt, ##__VA_ARGS__)
#define SOCK_F_WARN(fmt, ...)      NET_F_WARN("socket", fmt, ##__VA_ARGS__)
#define SOCK_F_ERROR(fmt, ...)     NET_F_ERROR("socket", fmt, ##__VA_ARGS__)
#define SOCK_F_FATAL(fmt, ...)     NET_F_FATAL("socket", fmt, ##__VA_ARGS__)
/*******Socket模块*********/

/*******TcpServer模块*********/
#define TCP_DEBUG()     NET_DEBUG("tcp_svr")
#define TCP_INFO()      NET_INFO("tcp_svr")
#define TCP_WARN()      NET_WARN("tcp_svr")
#define TCP_ERROR()     NET_ERROR("tcp_svr")
#define TCP_FATAL()     NET_FATAL("tcp_svr")

#define TCP_F_DEBUG(fmt, ...)     NET_F_DEBUG("tcp_svr", fmt, ##__VA_ARGS__)
#define TCP_F_INFO(fmt, ...)      NET_F_INFO("tcp_svr", fmt, ##__VA_ARGS__)
#define TCP_F_WARN(fmt, ...)      NET_F_WARN("tcp_svr", fmt, ##__VA_ARGS__)
#define TCP_F_ERROR(fmt, ...)     NET_F_ERROR("tcp_svr", fmt, ##__VA_ARGS__)
#define TCP_F_FATAL(fmt, ...)     NET_F_FATAL("tcp_svr", fmt, ##__VA_ARGS__)
/*******TcpServer模块*********/

/*******TcpConnect模块*********/
#define CONN_DEBUG()     NET_DEBUG("tcp_conn")
#define CONN_INFO()      NET_INFO("tcp_conn")
#define CONN_WARN()      NET_WARN("tcp_conn")
#define CONN_ERROR()     NET_ERROR("tcp_conn")
#define CONN_FATAL()     NET_FATAL("tcp_conn")

#define CONN_F_DEBUG(fmt, ...)     NET_F_DEBUG("tcp_conn", fmt, ##__VA_ARGS__)
#define CONN_F_INFO(fmt, ...)      NET_F_INFO("tcp_conn", fmt, ##__VA_ARGS__)
#define CONN_F_WARN(fmt, ...)      NET_F_WARN("tcp_conn", fmt, ##__VA_ARGS__)
#define CONN_F_ERROR(fmt, ...)     NET_F_ERROR("tcp_conn", fmt, ##__VA_ARGS__)
#define CONN_F_FATAL(fmt, ...)     NET_F_FATAL("tcp_conn", fmt, ##__VA_ARGS__)
/*******TcpConnect模块*********/

/*******Timer定时器模块*********/
#define TIMER_DEBUG()     NET_DEBUG("timer")
#define TIMER_INFO()      NET_INFO("timer")
#define TIMER_WARN()      NET_WARN("timer")
#define TIMER_ERROR()     NET_ERROR("timer")
#define TIMER_FATAL()     NET_FATAL("timer")

#define TIMER_F_DEBUG(fmt, ...)     NET_F_DEBUG("timer", fmt, ##__VA_ARGS__)
#define TIMER_F_INFO(fmt, ...)      NET_F_INFO("timer", fmt, ##__VA_ARGS__)
#define TIMER_F_WARN(fmt, ...)      NET_F_WARN("timer", fmt, ##__VA_ARGS__)
#define TIMER_F_ERROR(fmt, ...)     NET_F_ERROR("timer", fmt, ##__VA_ARGS__)
#define TIMER_F_FATAL(fmt, ...)     NET_F_FATAL("timer", fmt, ##__VA_ARGS__)
/*******Timer定时器模块*********/

/*******HttpRequest模块*********/
#define HTTP_DEBUG()     NET_DEBUG("http")
#define HTTP_INFO()      NET_INFO("http")
#define HTTP_WARN()      NET_WARN("http")
#define HTTP_ERROR()     NET_ERROR("http")
#define HTTP_FATAL()     NET_FATAL("http")

#define HTTP_F_DEBUG(fmt, ...)     NET_F_DEBUG("http", fmt, ##__VA_ARGS__)
#define HTTP_F_INFO(fmt, ...)      NET_F_INFO("http", fmt, ##__VA_ARGS__)
#define HTTP_F_WARN(fmt, ...)      NET_F_WARN("http", fmt, ##__VA_ARGS__)
#define HTTP_F_ERROR(fmt, ...)     NET_F_ERROR("http", fmt, ##__VA_ARGS__)
#define HTTP_F_FATAL(fmt, ...)     NET_F_FATAL("http", fmt, ##__VA_ARGS__)
/*******HttpRequest模块*********/



