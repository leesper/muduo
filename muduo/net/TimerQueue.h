// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include "muduo/base/Mutex.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Channel.h"

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
// 定时器队列
class TimerQueue : noncopyable
{
 public:
 // 定时器队列中执行的任务一定是挂在某个loop上的，也就是说会在IO线程执行
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  // 向定时器队列添加定时任务，返回该定时任务的TimerId
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

// 取消该定时任务
  void cancel(TimerId timerId);

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.
  // 时间戳和定时任务组成的项
  typedef std::pair<Timestamp, Timer*> Entry;
  // 定时任务集合
  typedef std::set<Entry> TimerList;
  // 活动定时任务
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  // 活动定时任务集合
  typedef std::set<ActiveTimer> ActiveTimerSet;

  // 在IO线程中添加定时任务
  void addTimerInLoop(Timer* timer);
  // 在IO线程中取消定时任务
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  // 一旦poller->poll()触发timerfd从阻塞中返回，channel分派IO事件时会回调该函数
  void handleRead();
  // move out all expired timers
  // 获取所有已经到期的定时任务
  std::vector<Entry> getExpired(Timestamp now);
  // 重置
  void reset(const std::vector<Entry>& expired, Timestamp now);

// 插入定时任务
  bool insert(Timer* timer);

// 所属事件循环
  EventLoop* loop_;
  // 用来与内核交互的定时器文件描述符和channel
  const int timerfd_;
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  // 按照到期时间排列的定时任务列表
  TimerList timers_;

  // for cancel()
  // 活动定时任务集合
  ActiveTimerSet activeTimers_;
  // 正在处理到期的定时任务
  bool callingExpiredTimers_; /* atomic */
  // 被取消的活动定时任务集合
  ActiveTimerSet cancelingTimers_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
