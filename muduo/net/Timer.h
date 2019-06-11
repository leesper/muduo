// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMER_H
#define MUDUO_NET_TIMER_H

#include "muduo/base/Atomic.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"

namespace muduo
{
namespace net
{

///
/// Internal class for timer event.
///
// Timer表示一个定时任务，老实说我觉得它应该叫TimerTask
class Timer : noncopyable
{
 public: // 构造函数一定要传入要调用的回调，要执行的时间when，如果是周期性的定时任务，就还要有间隔interval
  Timer(TimerCallback cb, Timestamp when, double interval)
    : callback_(std::move(cb)), // move语义传入回调
      expiration_(when), // 执行任务的时间点
      interval_(interval), // 时间间隔
      repeat_(interval > 0.0), // interval如果大于0说明是重复执行的定时任务，否则执行一次就嗝屁了
      sequence_(s_numCreated_.incrementAndGet()) // 给定时任务生成一个序列号
  { }

// 其实就是执行回调函数，回调函数会在IO线程执行，其实我觉得这样的设计要注意一个问题
// 如果有工作者线程池，然后不小心在IO线程执行了业务逻辑，可能会有data race
  void run() const
  {
    callback_();
  }

// 返回expiration_，即这个任务啥时候执行
  Timestamp expiration() const  { return expiration_; }
// 返回该任务是不是重复执行的任务
  bool repeat() const { return repeat_; }
  // 返回定时任务的序列号，这个想法不错，哈哈哈
  int64_t sequence() const { return sequence_; }
// 重新启动任务
  void restart(Timestamp now);
// 到目前为止创建了多少个定时任务了？
  static int64_t numCreated() { return s_numCreated_.get(); }

 private:
  const TimerCallback callback_; // 回调函数
  Timestamp expiration_; // 时间点
  const double interval_; // 时间间隔
  const bool repeat_; // 是否为重复执行的任务
  const int64_t sequence_; // 序列号

  static AtomicInt64 s_numCreated_; // 原子序列号生成器
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TIMER_H
