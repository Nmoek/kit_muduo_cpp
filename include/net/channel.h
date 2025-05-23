/**
 * @file channel.h
 * @brief
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 01:47:23
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_CHANNEL_H__
#define __KIT_CHANNEL_H__

#include "base/noncopyable.h"
#include "base/time_stamp.h"

#include <functional>
#include <memory>


namespace kit_muduo {

class EventLoop;

/**
 * @brief 该类是一个中间类，负责管理：监听fd、监听event、实际发生event、event对应的事件处理回调
 */
class Channel: Noncopyable
{
public:
    using ReadEventCb = std::function<void(TimeStamp)>;
    using EventCb = std::function<void()>;

    Channel(EventLoop *loop, int32_t fd);
    ~Channel() = default;

    /**
     * @brief 根据实际返回的_revent进行事件回调处理
     * @param[in] receiveTime
     */
    void handleEvent(TimeStamp receiveTime);

    void setReadCallback(ReadEventCb cb) { _readCallback = std::move(cb); }
    void setWriteCallback(EventCb cb) { _writeCallback = std::move(cb); }
    void setCloseCallback(EventCb cb) { _closeCallback = std::move(cb); }
    void setErrorCallback(EventCb cb) { _errorCallback = std::move(cb); }

    bool isNonEvent() const { return _events == kNonEvent; }
    bool isReading() const { return _events & kReadEvent; }
    bool isWriting() const { return _events & kWriteEvent; }

    void enableReading() { _events |= kReadEvent; update(); }
    void disableReading() { _events &= ~kReadEvent; update(); }

    void enableWriting() { _events |= kWriteEvent; update(); }
    void disableWriting() { _events &= ~kWriteEvent; update(); }

    void disableAll() { _events = kNonEvent; update(); }

    /**
     * @brief 防止channel被手动remove时，channel还在继续执行回调操作
     * @param[in] data 注意:这里使用引用是避免引用计数+1
     */
    void tie(const std::shared_ptr<void> &data) { _tie = data; _tied = true; }
    bool isTied() const { return _tied; }
    void remove();

    int32_t fd() const { return _fd; }
    int32_t events() const { return _events; }
    void setRevents(int32_t revents) { _revents = revents; }
    int32_t index() const { return _index; }
    void setIndex(int32_t index) { _index = index; }
    EventLoop* ownerLoop() const { return _loop; }


private:
    /**
     * @brief 向Poller更新事件
     */
    void update();

    /**
     * @brief 根据实际返回的_revent进行事件回调处理
     * @param[in] receiveTime
     */
    void handleEventWithGuard(TimeStamp receiveTime);

private:
    static const int32_t kNonEvent;
    static const int32_t kReadEvent;
    static const int32_t kWriteEvent;

private:
    /// @brief 事件循环
    EventLoop *_loop;
    /// @brief 通信fd
    int32_t _fd;
    /// @brief 监听事件集
    int32_t _events;
    /// @brief 实际发生事件集
    int32_t _revents;
    /// @brief 被哪个poller所使用
    int32_t _index;
    /// @brief 绑定自身，监视资源是否释放
    std::weak_ptr<void> _tie;
    /// @brief 是否绑定
    bool _tied;
    /// @brief 读事件回调函数
    ReadEventCb _readCallback;
    /// @brief 写事件回调函数
    EventCb _writeCallback;
    /// @brief 关闭事件回调函数
    EventCb _closeCallback;
    /// @brief 错误事件回调函数
    EventCb _errorCallback;

};


}   // kit_muduo
#endif