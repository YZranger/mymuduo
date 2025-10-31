#include "TcpConnection.h"
#include "Callbacks.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <memory>
#include <sys/types.h>
#include <sys/socket.h>

static EventLoop* CheckLoopNotNull(EventLoop *loop) {
    if(loop == nullptr) {
        LOG_FATAL("%s:%s:%d mainLoop is null!", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *Loop
                , const std::string &nameArg
                , int sockfd
                , const InetAddress& LocalAddr
                , const InetAddress& peerAddr)
    : loop_(CheckLoopNotNull(Loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop_, sockfd))
    , localAddr_(LocalAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64*1024*1024)
{
    // 下面给channel设置响应的回调函数，poller给channel通知感兴趣的事件发生了，channel会回调相应的操作函数
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1) 
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this) 
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this) 
    );
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this) 
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection() {
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d", name_.c_str(), channel_->fd(), static_cast<int>(state_));
}

void TcpConnection::handleRead(Timestamp receiveTime) {
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if(n > 0) {
        messageCallback_(shared_from_this(), &inputBuffer_, &receiveTime);
    } else if (n == 0) {
        handleClose();
    } else {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}
void TcpConnection::handleWrite() {
    if(channel_->isWriteEvent()) {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if(n > 0) {
            outputBuffer_.retrieve(n);
            if(outputBuffer_.readableBytes() == 0) {
                channel_->disableReading();
                if(writeCompleteCallback_) {
                    // 唤醒loop_对应的thread线程，执行回调
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if(state_ == kDisconnecting) {
                    shutdownInLoop();
                }
            }
        }else {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }else {
        LOG_ERROR("TcpConnection fd = %d is down, no more writing", channel_->fd());
    }
}
void TcpConnection::handleClose() {
    LOG_INFO("fd=%d state=%d", channel_->fd(), static_cast<int>(state_));
    setState(kDisconnected);
    channel_->diableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调
    closeCallback_(connPtr); // 关闭连接的回调
}
void TcpConnection::handleError() {
    int optval;
    socklen_t optlen = sizeof(optval);
    int err = 0;
    if(::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
        err = errno;
    } else {
        err = optval;
    }

    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d", name_.c_str(), err);
}

void TcpConnection::send(const std::string &buf) {
    if(state_ == kConnected) {
        if(loop_->isInLoopThread()) {
            sendInLoop(buf.c_str(), buf.size());
            
        }else {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
        }
    }
}

// 发送数据，应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，而且设置了水位回调
void TcpConnection::sendInLoop(const void* data, size_t len) {
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 之前调用过该connection的shutdown, 不能再进行发送了
    if(state_ == kDisconnected) {
        LOG_ERROR("disconnected, give up writing");
        return;
    }
    // 表示channel_第一次开始写数据，而且缓冲区没有待发送数据
    if(!channel_->isWriteEvent() && outputBuffer_.readableBytes() == 0) {
        nwrote = ::write(channel_->fd(), data, len);
        if(nwrote >= 0) {
            remaining = len - nwrote;
            if(remaining == 0 && writeCompleteCallback_) {
                // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件了
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }else {
            nwrote = 0;
            if(errno != EWOULDBLOCK) {
                LOG_ERROR("TcpConnection::sendInLoop");
                if(errno == EPIPE || errno == ECONNRESET) {
                    faultError = true;
                }
            }
        }

        // 说明当前这一次write,并没有把数据全部发送出去，剩余的数据需要保存到缓冲区，然后给channel设置EPOLLOUT的回调
        // poller发现tcp发送缓冲区有空间，会通知相应的sock-channel,调用handleWrite方法
        // 最终把发送缓冲区中的数据全部发送完成
        if(!faultError && remaining > 0) { 
            // 目前发送缓冲区待发送的数据
            size_t oldLen = outputBuffer_.readableBytes();
            if(oldLen + remaining >= highWaterMark_  && oldLen < highWaterMark_ && highWaterMarkCallback_) {
                loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
            }
            outputBuffer_.append((char*)data+nwrote, remaining);
            if(!channel_->isWriteEvent()) {
                channel_->enableWriting(); // 这里一定要注册channel的写事件，否则poller不会给channel同时epollout
            }
        }
    }
}

void TcpConnection::connectEstablished () {
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向Poller注册channel的epollin事件

    connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed() {
    if(state_ == kConnected) {
        setState(kDisconnected);
        channel_->diableAll();
        connectionCallback_(shared_from_this());
    }

    channel_->remove();
} 

void TcpConnection::shutdown() {
    if(state_ == kConnected) {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop() {
    if(!channel_->isWriteEvent()) { // 说明outputBuffer中的数据已经全部发送完成
        socket_->shutdownWrite(); // 关闭写端
    }
}