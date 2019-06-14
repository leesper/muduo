// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThreadPool.h"

#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

// 构造函数，传入的是线程池的名字，以及作为基础的baseLoop
EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg)
  : baseLoop_(baseLoop), // 初始化成员变量baseLoop
    name_(nameArg), // 初始化名称
    started_(false), // 线程还没启动
    numThreads_(0), // 0个其他事件循环线程
    next_(0) 
{
}

// 变量是创建在栈上的，不需要额外内存管理自动释放
EventLoopThreadPool::~EventLoopThreadPool()
{
  // Don't delete loop, it's stack variable
}

void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
  assert(!started_); // 从未开始过
  baseLoop_->assertInLoopThread(); // 函数必须在baseLoop_所在线程执行

  started_ = true; // 开始啦

  // 循环遍历
  for (int i = 0; i < numThreads_; ++i)
  {
    // 给每个新创建的EventLoopThread创建一个名字
    char buf[name_.size() + 32];
    snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
    // 创建EventLoopThread
    EventLoopThread* t = new EventLoopThread(cb, buf);
    // 新创建的EventLoopThread放到threads_中保存
    threads_.push_back(std::unique_ptr<EventLoopThread>(t));
    // 同时也保存一下对应的事件循环对象
    loops_.push_back(t->startLoop());
  }
  // 只有在线程池中没有其他线程的时候，才在baseLoop—_上执行一下回调
  if (numThreads_ == 0 && cb)
  {
    cb(baseLoop_);
  }
}

// round-robin策略获得下一个事件循环
EventLoop* EventLoopThreadPool::getNextLoop()
{
  // 这个操作一定是在baseLoop所在的线程执行的
  baseLoop_->assertInLoopThread();
  // 且这个线程池已经开始了
  assert(started_);
  // 先给loop赋初值为baseLoop
  EventLoop* loop = baseLoop_;

  // loops不为空
  if (!loops_.empty())
  {
    // round-robin
    // 取下一个loop
    loop = loops_[next_];
    ++next_;
    if (implicit_cast<size_t>(next_) >= loops_.size())
    {
      // 超过loop数量了，从头开始吧
      next_ = 0;
    }
  }
  // 如果我这个线程池啥玩意儿没有，就真的返回baseLoop了
  return loop;
}

EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
  // 这个函数也必须在baseLoop中执行，baseLoop就是主心骨
  baseLoop_->assertInLoopThread();
  EventLoop* loop = baseLoop_;

  if (!loops_.empty())
  {
    // 散列方法获取loop
    loop = loops_[hashCode % loops_.size()];
  }
  return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
  // 就没哪个函数不是在baseLoop线程中执行的
  baseLoop_->assertInLoopThread();
  assert(started_);
  if (loops_.empty())
  {
    // loops是空的，只好返回包含baseLoop一个元素的vector咯
    return std::vector<EventLoop*>(1, baseLoop_);
  }
  else
  {
    // 返回loops哈哈
    return loops_;
  }
}
