#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include <functional>
#include <memory>

/*
Channel理解为通道，封装了sockfd和其感兴趣的event,如EPOLLIN, EPOLLOUT事件
还绑定了poller返回的具体的事件
*/

class EventLoop;

class Channel : noncopyable {
public:
    using EventCallBack = std::function<void()>;
    using ReadEventCallBack = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // fd得到poller通知以后，处理事件的
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallBack cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallBack cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallBack cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallBack cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    // 返回fd当前的事件状态
    bool isNoneEvent() { return events_ == kNoneEvent; }
    bool isWriteEvent() { return events_ & kWriteEvent; }
    bool isReadEvent() { return events_ & kReadEvent; }

    // 设置fd当前的事件状态
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_  &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_  &= ~kWriteEvent; update(); }
    void diableAll() {events_ &= kNoneEvent; update();}

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop* ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_; // 事件循环
    const int fd_;  // fd,Poller监听的对象
    int events_; // 注册fd感兴趣的事件
    int revents_; // poller返回的具体发生的事件
    int index_;

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为channel通道里面能够知道fd最终发生的具体的事件，所以相应的回调函数也写在这里
    ReadEventCallBack readCallback_;
    EventCallBack writeCallback_;
    EventCallBack closeCallback_;
    EventCallBack errorCallback_;
};