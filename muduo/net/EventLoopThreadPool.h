// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_EVENTLOOPTHREADPOOL_H
#define MUDUO_NET_EVENTLOOPTHREADPOOL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <vector>

namespace muduo
{

namespace net
{

class EventLoop;
class EventLoopThread;

// 相当于是IO线程池
class EventLoopThreadPool : noncopyable
{
 public:
 // 线程初始化时回调的函数
  typedef std::function<void(EventLoop*)> ThreadInitCallback;
// 构造函数，其中传入baseLoop是用来accept新连接的，其他的loop用来处理IO事件
  EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg);
  // 析构函数
  ~EventLoopThreadPool();
// 设置线程的数量
  void setThreadNum(int numThreads) { numThreads_ = numThreads; }
  // 启动线程，启动时传入回调
  void start(const ThreadInitCallback& cb = ThreadInitCallback());

  // valid after calling start()
  /// round-robin
  // 以round-robin策略分配事件循环，这里可以改进一下，以不同策略分配loop
  EventLoop* getNextLoop();

  /// with the same hash code, it will always return the same EventLoop
// 用hash值来散列分配loop
  EventLoop* getLoopForHash(size_t hashCode);
// 获取所有的事件循环
  std::vector<EventLoop*> getAllLoops();

// 线程池是否已经开始
  bool started() const
  { return started_; }
// 线程池名称
  const string& name() const
  { return name_; }

 private:
// 基础事件循环
  EventLoop* baseLoop_;
  string name_; // 名称
  bool started_; // 线程池是否已启动
  int numThreads_; // 线程数量
  int next_; // 下一个的索引
  std::vector<std::unique_ptr<EventLoopThread>> threads_; // 事件循环线程集合
  std::vector<EventLoop*> loops_; // 事件循环集合
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREADPOOL_H
