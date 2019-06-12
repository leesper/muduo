// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"

namespace muduo
{
namespace net
{

class EventLoop;

// 专门为EventLoop准备的线程类
class EventLoopThread : noncopyable
{
 public:
 // ThreadInitCallback是线程初始化时的回调函数
  typedef std::function<void(EventLoop*)> ThreadInitCallback;
// 构造函数
  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
// 析构函数
  ~EventLoopThread();
// 返回对应的EventLoop事件循环
  EventLoop* startLoop();

 private:
 // 线程中执行的函数
  void threadFunc();

  EventLoop* loop_ GUARDED_BY(mutex_);
  bool exiting_; // 标志位：正在退出
  Thread thread_; // 封装好的Pthread线程对象
  MutexLock mutex_; // 互斥量，保护loop_的
  Condition cond_ GUARDED_BY(mutex_); // 条件变量
  ThreadInitCallback callback_; // 回调函数
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

