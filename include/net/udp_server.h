/**
 * @file udp_server.h
 * @brief UDP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-29 04:37:31
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_UDP_SERVER_H__
#define __KIT_UDP_SERVER_H__

#include "base/noncopyable.h"
#include "net/call_backs.h"
#include "net/udp_datagram.h"
#include "base/thread_pool.h"

#include <memory>

namespace kit_muduo {

class EventLoop;
class InetAddress;

class UdpServer: Noncopyable, public std::enable_shared_from_this<UdpServer>
{
public:
    /**
     * @brief 构造UDP服务器
     * @param[in] loop 所属IO事件循环
     * @param[in] addr 本地监听地址
     * @param[in] name 服务器名称
     */
    UdpServer(EventLoop *loop, const InetAddress &addr, const std::string &name = "");

    ~UdpServer();

    EventLoop *getLoop() const {return base_loop_; }

    /**
     * @brief 启动UDP服务器
     */
    void start();

    /**
     * @brief 设置收到UDP报文后的业务回调
     * @param[in] cb 用户业务回调
     */
    void setMessageCallback(const UdpMessageCb &cb) { message_callback_ = std::move(cb); }

private:
    /**
     * @brief 处理底层收到的UDP报文，并转交给上层回调
     * @param[in] message 收到的UDP报文
     * @param[in] peer_addr 对端地址
     * @param[in] time_stamp 收包时间
     */
    void newDatagram(const std::vector<uint8_t>& message, const InetAddress& peer_addr, TimeStamp time_stamp);

private:
    EventLoop *base_loop_;
    std::string name_;
    const InetAddress local_addr_;
    std::unique_ptr<UdpDatagram> acceptor_;
    std::unique_ptr<ThreadPool> work_thread_pool_;

    std::atomic_int started_;

    UdpMessageCb message_callback_;
};



}
#endif //__KIT_UDP_SERVER_H__
