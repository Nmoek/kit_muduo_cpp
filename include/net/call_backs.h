/**
 * @file call_backs.h
 * @brief 回调函数声明
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 21:18:20
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_CALL_BACKS_H__
#define __KIT_CALL_BACKS_H__

#include <functional>
#include <memory>

namespace kit_muduo {

class TcpConnection;
class Buffer;
class TimeStamp;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

//定时器回调函数
using TimerCb = std::function<void()>;

using ConnectionCb = std::function<void(const TcpConnectionPtr&)>;
using CloseCb = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCb =  std::function<void(const TcpConnectionPtr&)>;
// 写入高水位回调, 目的: 收发速率不对等时, 控制写入速率
using HighWaterMarkCb = std::function<void(const TcpConnectionPtr&, size_t)>;

using MessageCb = std::function<void(const TcpConnectionPtr&, Buffer*, TimeStamp)>;

}   //kit_muduo
#endif