// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include "muduo/net/TimerQueue.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{

// 创建定时器文件描述符，通过内核获得一个非阻塞的fd
int createTimerfd()
{
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}

// 计算一下when离当下还有多少时间
struct timespec howMuchTimeFromNow(Timestamp when)
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
  {
    microseconds = 100;
  }
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

// 读timerfd
void readTimerfd(int timerfd, Timestamp now)
{
  uint64_t howmany;
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

// 重置timerfd，让其在新的时间点上触发
void resetTimerfd(int timerfd, Timestamp expiration)
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  memZero(&newValue, sizeof newValue);
  memZero(&oldValue, sizeof oldValue);
  newValue.it_value = howMuchTimeFromNow(expiration);
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret)
  {
    LOG_SYSERR << "timerfd_settime()";
  }
}

}  // namespace detail
}  // namespace net
}  // namespace muduo

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

// 构造函数，传入的是定时器队列依靠的事件循环对象
// 所有的定时任务都会在这个事件循环上执行
TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop), //赋值事件循环
    timerfd_(createTimerfd()),// 创建定时器文件描述符
    timerfdChannel_(loop, timerfd_), // 定时器文件描述符对应的channel
    timers_(), // 空集合
    callingExpiredTimers_(false) // 标志位
{
  // 设置触发定时器时分派IO事件要执行的函数
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  // 对读事件感兴趣
  timerfdChannel_.enableReading();
}

// 析构函数
TimerQueue::~TimerQueue()
{
  // 对所有的IO事件都不感兴趣了
  timerfdChannel_.disableAll();
  // 把我从EventLoop中删除掉
  timerfdChannel_.remove();
  // 关闭timerfd，释放操作系统资源
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  // 遍历定时任务，析构全部的second，即Timer定时任务
  for (const Entry& timer : timers_)
  {
    delete timer.second;
  }
}

// 添加定时任务
TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  // 首先，创建一个定时任务对象
  Timer* timer = new Timer(std::move(cb), when, interval);
  // 把添加定时任务的工作安排给IO线程去处理，具体的工作由addTimerInLoop去做
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));
  // 返回对应的TimerId，留作将来取消定时任务使用
  return TimerId(timer, timer->sequence());
}

// 取消定时任务，这里只是简单的把它放到了IO线程中去做，实际上是cancelInLoop()在执行
void TimerQueue::cancel(TimerId timerId)
{
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

// IO线程中真正执行添加定时任务的函数，insert()函数将定时任务插入到队列中
// 如果最早的那个发生了变化，就resetTimerfd()重置定时器
void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
    resetTimerfd(timerfd_, timer->expiration());
  }
}

// IO线程中真正执行取消定时任务的函数
void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  // 创建ActiveTimer
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  // 尝试查找
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  // 如果在找到了，就把原来那个先删掉
  if (it != activeTimers_.end())
  {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_) // 否则如果正在调用到期的定时任务，就先把timer塞到cancelingTimers里面
  {
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  // 读timerfd
  readTimerfd(timerfd_, now);
  // 获取所有的到期定时任务
  std::vector<Entry> expired = getExpired(now);
  // 开始调用定时任务了，先设置标志位
  callingExpiredTimers_ = true;
  // 先清空被取消掉的定时任务
  cancelingTimers_.clear();
  // safe to callback outside critical section
  // 挨个调用定时任务中的回调
  for (const Entry& it : expired)
  {
    it.second->run();
  }
  // 处理完毕，改回去
  callingExpiredTimers_ = false;
  // 重置定时任务，主要是把周期性的任务再更新下放回队列
  reset(expired, now);
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  // 这段代码大致意思是说，找到所有到期了的定时任务放到expired
  // 然后再把它们从timers_中删掉
  assert(timers_.size() == activeTimers_.size());
  std::vector<Entry> expired;
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  TimerList::iterator end = timers_.lower_bound(sentry);
  assert(end == timers_.end() || now < end->first);
  std::copy(timers_.begin(), end, back_inserter(expired));
  timers_.erase(timers_.begin(), end);
  // 然后再从activeTimers中删掉，始终保持timers_.size() == activeTimers_.size()的断言不变
  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    size_t n = activeTimers_.erase(timer);
    assert(n == 1); (void)n;
  }

  assert(timers_.size() == activeTimers_.size());
  return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;
// 遍历所有已到期的任务
  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    if (it.second->repeat() // 如果这个任务是周期性重复的，且没有被cancel
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it.second->restart(now); // 重启它，然后插入队列中
      insert(it.second);
    }
    else // 否则彻底释放资源
    {
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();
  }
// 获得下一个最早的定时时间，然后重置timerfd
  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  bool earliestChanged = false;
  // 先得到定时任务的执行时间
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  // 如果timers_本来就是空的，或者when比第一个定时任务时间都小
  // 说明它插入后一定会改变最早任务的时间，那么earliestChanged就为true
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  {
    // 插入这个定时任务到timers_中
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  {
    // 插入这个定时任务到activeTimers_中
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }
// 断言必须仍然保持
  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}

