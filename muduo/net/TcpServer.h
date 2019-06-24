// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPSERVER_H
#define MUDUO_NET_TCPSERVER_H

#include "muduo/base/Atomic.h"
#include "muduo/base/Types.h"
#include "muduo/net/TcpConnection.h"

#include <map>

namespace muduo
{
namespace net
{

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

///
/// TCP server, supports single-threaded and thread-pool models.
///
/// This is an interface class, so don't expose too much details.
// TCP服务器类
class TcpServer : noncopyable
{
 public:
 // 事件循环启动时回调的函数
  typedef std::function<void(EventLoop*)> ThreadInitCallback;
  // 端口重用选项
  enum Option
  {
    kNoReusePort,
    kReusePort,
  };

  //TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  TcpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const string& nameArg,
            Option option = kNoReusePort);
  ~TcpServer();  // force out-line dtor, for std::unique_ptr members.

  const string& ipPort() const { return ipPort_; }
  const string& name() const { return name_; }
  EventLoop* getLoop() const { return loop_; }

  /// Set the number of threads for handling input.
  ///
  /// Always accepts new connection in loop's thread.
  /// Must be called before @c start
  /// @param numThreads
  /// - 0 means all I/O in loop's thread, no thread will created.
  ///   this is the default value.
  /// - 1 means all I/O in another thread.
  /// - N means a thread pool with N threads, new connections
  ///   are assigned on a round-robin basis.
  // 设置IO线程池中线程数量
  void setThreadNum(int numThreads);
  void setThreadInitCallback(const ThreadInitCallback& cb)
  { threadInitCallback_ = cb; }
  /// valid after calling start()
  // 获取IO线程池
  std::shared_ptr<EventLoopThreadPool> threadPool()
  { return threadPool_; }

  /// Starts the server if it's not listenning.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  // 启动服务器
  void start();

  /// Set connection callback.
  /// Not thread safe.
 // 设置连接建立时回调
  void setConnectionCallback(const ConnectionCallback& cb)
  { connectionCallback_ = cb; }

  /// Set message callback.
  /// Not thread safe.
  // 设置消息到来时回调
  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  // 设置写完成后的回调
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

 private:
  /// Not thread safe, but in loop
  // 创建新连接的业务逻辑
  void newConnection(int sockfd, const InetAddress& peerAddr);
  /// Thread safe.
  // 删除连接的业务逻辑
  void removeConnection(const TcpConnectionPtr& conn);
  /// Not thread safe, but in loop
  // 在IO线程中删除连接
  void removeConnectionInLoop(const TcpConnectionPtr& conn);

// 连接管理
  typedef std::map<string, TcpConnectionPtr> ConnectionMap;

  EventLoop* loop_;  // the acceptor loop 看见没，acceptor loop
  const string ipPort_;
  const string name_;
  std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor
  std::shared_ptr<EventLoopThreadPool> threadPool_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;
  AtomicInt32 started_;
  // always in loop thread
  int nextConnId_;
  ConnectionMap connections_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
