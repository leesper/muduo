// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThread.h"

#include "muduo/net/EventLoop.h"

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL), // 线程创建时是没有事件循环的
    exiting_(false), // 置为false
    thread_(std::bind(&EventLoopThread::threadFunc, this), name), // 初始化线程对象，传入要执行的函数
    mutex_(), // 互斥量构造
    cond_(mutex_), // 条件变量构造
    callback_(cb) // 回调函数构造
{
}

EventLoopThread::~EventLoopThread()
{
  // 硕哥好像对这种置状态变量的事情有独钟啊
  exiting_ = true;
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit(); // 告诉事件循环停止执行
    thread_.join(); // 线程等待所有该做完的事情做完了再退出
  }
}

EventLoop* EventLoopThread::startLoop()
{
  // 线程之前没启动过吧？其实有更好的解法，参照Go语言的sync.Once()
  assert(!thread_.started()); 
  // 启动线程咯，线程开始执行threadFunc了
  thread_.start();

  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)
    {
      // loop_还没构造完呢，再等等
      cond_.wait();
    }
    // 构造完毕，赋值吧
    loop = loop_;
  }

  return loop;
}

void EventLoopThread::threadFunc()
{
  EventLoop loop;
// 线程在执行的时候，先回调一下初始化函数
  if (callback_)
  {
    callback_(&loop);
  }

  {
    // 锁住，给loop_赋值成功了再通知其他线程我好了
    MutexLockGuard lock(mutex_);
    loop_ = &loop;
    cond_.notify();
  }

  // 线程中运行事件循环咯
  loop.loop();
  //assert(exiting_);
  // 从事件循环退出，说明要结束了
  MutexLockGuard lock(mutex_);
  loop_ = NULL;
}

