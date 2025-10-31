#include "Acceptor.h"
#include "Logger.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

static int createNonblocking() {
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_TCP);
    if(sockfd < 0) {
        LOG_FATAL("%s:%s:%d listen socket create error: %d", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop* loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking()) // 创建套接字
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true); // TODO:reuse Address是什么
    acceptSocket_.setReusePort(reuseport);
    acceptSocket_.bindAddress(listenAddr); // bind绑定套接字到特定的ip:port
    // TODO:TcpServer::start() Acceptor.listen 有新用户的连接，要执行一个回调（connfd=>channel=>wakeup subloop）
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor() {
    acceptChannel_.diableAll();
    acceptChannel_.remove();
}

void Acceptor::listen() {
    listenning_ = true;
    acceptSocket_.listen(); // listen
    acceptChannel_.enableReading(); // acceptChannel_=>Poller
}

// listenfd有事件发生了，就是有新用户连接了
void Acceptor::handleRead() {
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if(connfd >= 0) {
        if(newConnectionCallback_) 
        {
            newConnectionCallback_(connfd, peerAddr); // 轮询找到subloop,唤醒，分发当前的新客户端的channel
        } else {
            ::close(connfd);
        }
    } else {
        LOG_ERROR("%s:%s:%d accept error: %d", __FILE__, __FUNCTION__, __LINE__, errno);
        if(errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reach limit", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}