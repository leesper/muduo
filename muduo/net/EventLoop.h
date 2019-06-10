// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/TimerId.h"

namespace muduo
{
namespace net
{

class Channel;
class Poller;
class TimerQueue;

///
/// Reactor, at most one per thread.
///
/// This is an interface class, so don't expose too much details.
// EventLoop就是传说中的Reactor，即反应器
class EventLoop : noncopyable
{
 public:
 // 跨线程调用时从其他线程塞进来在EventLoop所在IO线程中执行的函数对象
  typedef std::function<void()> Functor;
// 构造函数
  EventLoop();
// 析构函数
  ~EventLoop();  // force out-line dtor, for std::unique_ptr members.

  ///
  /// Loops forever.
  ///
  /// Must be called in the same thread as creation of the object.
  ///
  // 运行IO事件循环的主函数
  void loop();

  /// Quits loop.
  ///
  /// This is not 100% thread safe, if you call through a raw pointer,
  /// better to call through shared_ptr<EventLoop> for 100% safety.
  // 退出事件循环
  void quit();

  ///
  /// Time when poll returns, usually means data arrival.
  ///
  // 返回poll()调用从阻塞返回的时间
  Timestamp pollReturnTime() const { return pollReturnTime_; }

// 返回EventLoop迭代了多少次
  int64_t iteration() const { return iteration_; }

  /// Runs callback immediately in the loop thread.
  /// It wakes up the loop, and run the cb.
  /// If in the same loop thread, cb is run within the function.
  /// Safe to call from other threads.
  // 在IO线程中运行函数对象
  void runInLoop(Functor cb);
  /// Queues callback in the loop thread.
  /// Runs after finish pooling.
  /// Safe to call from other threads.
  // 将函数对象塞入队列，等事件循环分派完IO事件后开始执行函数对象
  void queueInLoop(Functor cb);

// 返回队列中存有的函数对象的个数
  size_t queueSize() const;

  // timers

  ///
  /// Runs callback at 'time'.
  /// Safe to call from other threads.
  ///
  // 在某个时间点运行定时函数
  TimerId runAt(Timestamp time, TimerCallback cb);
  ///
  /// Runs callback after @c delay seconds.
  /// Safe to call from other threads.
  ///
  // 在某个时间之后运行定时函数
  TimerId runAfter(double delay, TimerCallback cb);
  ///
  /// Runs callback every @c interval seconds.
  /// Safe to call from other threads.
  ///
  // 每隔一定的时间运行一次定时函数
  TimerId runEvery(double interval, TimerCallback cb);
  ///
  /// Cancels the timer.
  /// Safe to call from other threads.
  ///
  // 取消定时任务
  void cancel(TimerId timerId);

  // internal usage
  // 用eventfd唤醒阻塞的事件循环
  void wakeup();
  // 更新channel，即EventHandler
  void updateChannel(Channel* channel);
  // 从EventLoop中删掉EventHandler，不再使用
  void removeChannel(Channel* channel);
  // 检查EventHandler是否在这个EventLoop中
  bool hasChannel(Channel* channel);

  // pid_t threadId() const { return threadId_; }
  // 如果当前线程并不在EventLoop所处线程中，崩溃退出
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      abortNotInLoopThread();
    }
  }
  // 判断当前线程ID是否等同于EventLoop线程ID
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  // bool callingPendingFunctors() const { return callingPendingFunctors_; }
  // 判断是否在进行事件的处理
  bool eventHandling() const { return eventHandling_; }

// 设置上下文对象
  void setContext(const boost::any& context)
  { context_ = context; }

// 获取上下文对象
  const boost::any& getContext() const
  { return context_; }

// 获取可修改的上下文对象
  boost::any* getMutableContext()
  { return &context_; }

// 返回当前线程的EventLoop
  static EventLoop* getEventLoopOfCurrentThread();

 private:
 // 当前线程不为EventLoop所在线程时断言退出
  void abortNotInLoopThread();
  // 传递给channel的回调函数，用来读取写入到eventfd的一个字节，IO线程从poll阻塞中返回
  void handleRead();  // waked up
  // 事件循环在分派完IO事件，回调用户注册的函数后，开始执行跨线程调用发来的函数对象
  void doPendingFunctors();

  // 打印activeChannels中的所有对象
  void printActiveChannels() const; // DEBUG

  typedef std::vector<Channel*> ChannelList;

// 是否正在执行IO事件循环
  bool looping_; /* atomic */
  // 退出标志
  std::atomic<bool> quit_;
  // 事件分派标志
  bool eventHandling_; /* atomic */
  // 处理跨线程调用标志
  bool callingPendingFunctors_; /* atomic */
  // 迭代次数
  int64_t iteration_;
  // 线程标识
  const pid_t threadId_;
  // poll()调用返回时间
  Timestamp pollReturnTime_;
  // 同步事件分派器
  std::unique_ptr<Poller> poller_;
  // 存放定时任务的定时器队列
  std::unique_ptr<TimerQueue> timerQueue_;
  // 唤醒EventLoop所在线程的eventfd
  int wakeupFd_;
  // unlike in TimerQueue, which is an internal class,
  // we don't expose Channel to client.
  // wakeupFd对应的wakeupChannel
  std::unique_ptr<Channel> wakeupChannel_;
  // 上下文对象
  boost::any context_;

  // scratch variables
  // poll()函数返回的具有IO事件的channel集合
  ChannelList activeChannels_;
  // 当前活动的channel
  Channel* currentActiveChannel_;

  // 互斥量保护的函数对象集合
  mutable MutexLock mutex_;
  std::vector<Functor> pendingFunctors_ GUARDED_BY(mutex_);
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOP_H
