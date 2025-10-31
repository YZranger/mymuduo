#include "Channel.h"
#include <memory>
#include <sys/epoll.h>
#include "EventLoop.h"
#include "Logger.h"

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd) 
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
{

}

Channel::~Channel() 
{

}

// 这里的obj可以去除&，不会有语义问题也不会造成内存泄漏的问题，但是会多出两次shared_ptr的
// 创建和删除的开销，因为按值传递会进行一次拷贝构造，离开局部作用域会调用一次析构
// Channel的tie方法什么时候调用的:一个TcpConnection新连接创建的时候TcpConnection => Channel
void Channel::tie(const std::shared_ptr<void> &obj) 
{
    tie_ = obj;
    tied_ = true;
}

/*
当改变Channel所表示的fd的events事件后，update需要在poller里面更改fd相应的事件epoll_ctl
EventLoop => ChannelList Poller,Channel本身并不包含poller对象，因此需要通过它所属的
EventLoop对象来做到这件事
*/
void Channel::update() {
    loop_->updateChannel(this);
}

void Channel::remove() {
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime) {
    std::shared_ptr<void> guard;
    if(tied_) {
        guard = tie_.lock();
        if(guard) {
            handleEventWithGuard(receiveTime);
        }
    } else { 
        handleEventWithGuard(receiveTime);
    }
}

// 根据poller返回的具体的事件，由channel去调用相应的回调函数
void Channel::handleEventWithGuard(Timestamp receiveTime) {
    LOG_INFO("channel handleEvent revents:%d\n", revents_);
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if(closeCallback_) {
            closeCallback_();
        }
    }

    if(revents_ & EPOLLERR) {
        if(errorCallback_) {
            errorCallback_();
        }
    }

    if(revents_ & (EPOLLIN | EPOLLPRI)) {
        if(readCallback_) {
            readCallback_(receiveTime);
        }
    }

    if(revents_ & EPOLLOUT) {
        if(writeCallback_) {
            writeCallback_();
        }
    }
}