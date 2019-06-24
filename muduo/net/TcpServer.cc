// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Acceptor.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThreadPool.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)), // 初始化loop
    ipPort_(listenAddr.toIpPort()), // 初始化ip地址和端口
    name_(nameArg), // 初始化名称
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)), // 初始化acceptor
    threadPool_(new EventLoopThreadPool(loop, name_)), // 初始化线程池
    connectionCallback_(defaultConnectionCallback), // 设置连接回调
    messageCallback_(defaultMessageCallback), // 设置消息回调
    nextConnId_(1) // 用整数来管理连接，方便知道连接建立了多少次
{
  // acceptor在readable的时候会回调newConnection函数
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
  // 在IO线程中发生析构
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";
// 遍历所有的连接，然后在conn所在的线程中执行connectDestroyed，防止data race
  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

// 设置线程数量
void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
  if (started_.getAndSet(1) == 0) // 控制只执行一次
  {
    // 启动线程池，传入线程初始化函数
    threadPool_->start(threadInitCallback_);
// acceptor没有开始监听
    assert(!acceptor_->listenning());
    // 在loop中监听网络连接
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}

// 这个函数在Acceptor中获得新连接的文件描述符和地址后被回调到
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread(); // 所以它是在loop_中执行的
  EventLoop* ioLoop = threadPool_->getNextLoop(); // 给新来的分配个好去处
  char buf[64];
  // 以服务名，IP，端口号，ID的方式命名
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;
// 输出一行日志，表明新创建连接的对端IP地址和端口号
  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  // 创建代表新连接的对象，传入一个ioLoop来负责处理这个连接的IO事件
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName, // 这个是用来命名的
                                          sockfd, // 连接对应的套接字描述符
                                          localAddr, // 本端地址
                                          peerAddr)); // 对端地址
  // 连接管理
  connections_[connName] = conn;
  // 设置连接回调
  conn->setConnectionCallback(connectionCallback_);
  // 设置消息回调
  conn->setMessageCallback(messageCallback_);
  // 设置写完成时回调
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  // 设置关闭时的回调，也就是删除连接，释放所有相关的资源
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
  // 在ioLoop中运行对应连接的connectEstablished函数
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

// 其实就是在loop_线程中运行removeConnectionInLoop函数
void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  // 在什么地方加入的就在什么地方删除
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  // 然后拿到这个连接所在的那个ioLoop
  EventLoop* ioLoop = conn->getLoop();
  // 在这个线程中执行connectDestroyed函数
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
}

