#pragma once 

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>
#include <string>

class Channel;
class EventLoop;
class Socket;

/*
* TcpServer => Acceptor => 有一个新用户的连接，通过aceept函数拿到connfd => 注册到对应的subloop的poller上面被监听
* => TcpConnection => 设置回调 => Channel => Poller => 执行channel的回调
*/

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop *Loop
                , const std::string &nameArg
                , int sockfd
                , const InetAddress& LocalAddr
                , const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; };
    const std::string& name() const { return name_ ; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress () const { return peerAddr_; }
    
    bool connected() const { return state_ == kConnected; }

    // 发送数据
    void send(const std::string &buf);
    // 关闭连接
    void shutdown();

    // 给channel设置各种回调
    void setConnectionCallback(const ConnectionCallback& cb) {
        connectionCallback_ = cb;
    }
    void setMessageCallback(const MessageCallback& cb) {
        messageCallback_ = cb;
    }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) {
        writeCompleteCallback_ = cb;
    }
    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb) {
        highWaterMarkCallback_ = cb;
    }
    void setCloseCallback(const CloseCallback& cb) {
        closeCallback_ = cb;
    }

    void connectEstablished();
    void connectDestroyed();

private:
    enum StateE {kDisconnected, kConnecting, kConnected, kDisconnecting};
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void* message, size_t len);
    
    void shutdownInLoop();

    void setState(StateE state) { state_ = state; }

    EventLoop *loop_;
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;

    size_t highWaterMark_;

    Buffer inputBuffer_; // 接收数据的缓冲区
    Buffer outputBuffer_; // 发送数据的缓冲区
};