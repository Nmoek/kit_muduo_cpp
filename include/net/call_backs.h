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

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kit_muduo {

class TcpConnection;
class UdpDatagram;
class Buffer;
class TimeStamp;
class TcpServer;
class InetAddress;

class Timer;

namespace http{
class HttpRequest;
class HttpResponse;
class HttpContext;
class HttpServer;
};


using TcpServerPtr = std::shared_ptr<TcpServer>;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using UdpDatagramPtr = std::shared_ptr<UdpDatagram>;
using ContextPtr = std::shared_ptr<void>;

/**********HTTP************/
using HttpServerPtr = std::shared_ptr<http::HttpServer>;
using HttpContextPtr = std::shared_ptr<http::HttpContext>;
using HttpRequestPtr = std::shared_ptr<http::HttpRequest>;
using HttpResponsePtr = std::shared_ptr<http::HttpResponse>;
/**********HTTP************/



/**********Timer***********/
using TimerPtr = std::shared_ptr<Timer>;
//定时器回调函数
using TimerCb = std::function<void()>;

/**********Timer***********/


using ConnectionCb = std::function<void(const TcpConnectionPtr&)>;
using CloseCb = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCb =  std::function<void(const TcpConnectionPtr&)>;
// 写入高水位回调, 目的: 收发速率不对等时, 控制写入速率
using HighWaterMarkCb = std::function<void(const TcpConnectionPtr&, size_t)>;
using MessageCb = std::function<void(const TcpConnectionPtr&, Buffer*, TimeStamp)>;

using UdpMessageCb = std::function<void(const std::vector<uint8_t>&, const InetAddress&, TimeStamp)>;
using UdpWriteCompleteCb = std::function<void()>;
using UdpErrorCb = std::function<void(int32_t, const InetAddress&)>;

}   //kit_muduo
#endif
