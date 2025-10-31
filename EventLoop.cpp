#include "EventLoop.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include "Logger.h"
#include <errno.h>
#include "Poller.h"
#include "Channel.h"


// 防止一个线程创建多个EventLoop
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建wakeupfd ,用来notify唤醒subReactor处理新来的channel
int createEventfd() {
    int evtfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0) {
        LOG_FATAL("eventfd error:%d", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
    , currentActiveChannel_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d", this, threadId_);
    if(t_loopInThisThread) {
        LOG_FATAL("Another EventLoop %p exists in this thread %d", t_loopInThisThread, threadId_);
    }else {
        t_loopInThisThread = this;
    }

    // 设置wakeupfd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->diableAll();
    wakeupChannel_->remove();
    close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::handleRead() {
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one)) {
        LOG_ERROR("EventLoop::handleRead() reads %ld bytes instead of 8", n);
    }
}

void EventLoop::loop() {
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start loop", this);

    while(!quit_) {
        activeChannels_.clear();
        // 监听两类fd 一种是client的fd 一种是wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for(Channel *channel : activeChannels_) {
            // Poller监听哪些channel发生事件了，然后上报给EventLoop, 通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        /*
        * IO线程 mainLoop accept fd => channel subLoop添加新连接需要监听I/O事件的fd
        * mainLoop先注册一个回调cb(需要subLoop来执行) wakeup subloop以后执行下面的方法，执行之前mainLoop注册的回调函数
        */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping.", this);
    looping_ = false;
}

// 推出事件循环 1.loop在自己的线程中调用quit 2.在非loop的线程中，调用loop的quit
void EventLoop::quit() {

    quit_ = true;

    if (!isInLoopThread()) {    // 如果是在其他线程中，调用的quit 在一个subLoop中，调用了mainLoop的quit
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb) {
    if(isInLoopThread()) { // 在当前的loop线程中执行cb
        cb();
    }else { // 在非loop中执行cb，那就需要唤醒loop所在线程执行cb
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的，需要执行上面回调操作的loop的线程了
    // || callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又被其他的线程加入了新的回调，如果不重新wakeup,在执行完当前回调之后依旧会阻塞
    if(!isInLoopThread() || callingPendingFunctors_) { // TODO:callingPendingFuntors_的逻辑待解释
        wakeup(); // 唤醒loop所在线程
    }
}

void EventLoop::updateChannel(Channel *channel) {
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel) {
    return poller_->hasChannel(channel);
}

// 用来唤醒loop所在的线程的，向wakeupfd_写一个数据,当前loop就会被唤醒
void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one)) {
        LOG_ERROR("EventLoop::wakeup() write %ld bytes instead of 8 in wakeupFd_", n);
    }

}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    } 

    for(const Functor &functor : functors) {
        functor();  // 执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;
}