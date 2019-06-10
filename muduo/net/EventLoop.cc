// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoop.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/Channel.h"
#include "muduo/net/Poller.h"
#include "muduo/net/SocketsOps.h"
#include "muduo/net/TimerQueue.h"

#include <algorithm>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
// thread local 数据，属于该线程的EventLoop
__thread EventLoop* t_loopInThisThread = 0;

// 常量，poll()调用等待时间，10s
const int kPollTimeMs = 10000;

// 创建eventfd，用户态事件，通过它唤醒EventLoop
int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

// 忽略PIPE信号
#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}  // namespace

// 返回当前线程的EventLoop对象
EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

// EventLoop() 构造函数
EventLoop::EventLoop()
  : looping_(false), // 初始化looping_
    quit_(false), // 初始化quit_
    eventHandling_(false), // 初始化eventHandling_
    callingPendingFunctors_(false), // 初始化callingPendingFunctors_
    iteration_(0), // 初始化迭代次数
    threadId_(CurrentThread::tid()), // 获取当前线程的ID
    poller_(Poller::newDefaultPoller(this)), // 初始化poller_
    timerQueue_(new TimerQueue(this)), // 初始化定时器队列，定时任务在EventLoop中执行
    wakeupFd_(createEventfd()), // 初始化wakeupFd_
    wakeupChannel_(new Channel(this, wakeupFd_)), // 初始化wakeupChannel_
    currentActiveChannel_(NULL) // 初始化currentActiveChannel_
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  // 如果t_loopInThisThread不为空，说明该线程已经创建了EventLoop，每个IO线程只允许有最多一个EventLoop
  if (t_loopInThisThread)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    // 否则给t_loopInThisThread赋值，防止错误地再次在该线程中创建EventLoop
    t_loopInThisThread = this;
  }
  // 给wakeupChannel设置读事件回调为EventLoop::handleRead()
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  // 打开开关，允许响应读事件并回调handleRead()函数
  wakeupChannel_->enableReading();
}

// 析构函数
EventLoop::~EventLoop()
{
  // 析构时打印日志
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  // 关闭所有开关，不响应任何读写事件
  wakeupChannel_->disableAll();
  // 从EventLoop中删除wakeupChannel_
  // 实际上在wakeupChannel_内部的EventLoop调用了removeChannel()，然后调用了poller_.removeChannel()
  wakeupChannel_->remove();
  // 关闭wakeupFd_释放资源
  ::close(wakeupFd_);
  // 清空thread local 数据
  t_loopInThisThread = NULL;
}

// 主事件循环
void EventLoop::loop()
{
  assert(!looping_); // 断言事件循环并未执行
  assertInLoopThread(); // 断言执行该函数的线程就是IO线程
  looping_ = true; // 置标志位为true
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    // 清空活动的channel集合
    activeChannels_.clear();
    // 调用同步事件分派器，poll()函数会阻塞，直到有IO事件发生
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_; // 执行过多少次poll()的计数
    if (Logger::logLevel() <= Logger::TRACE)
    {
      // 打印所有的活动channel信息，方便追踪和调试
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true; // 表明正在进行事件分派
    // 循环遍历所有活动channel，调用handleEvent()进行IO事件分派
    // 也就是回调用户注册的处理函数handleXXX()
    for (Channel* channel : activeChannels_)
    {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent(pollReturnTime_);
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    // 处理完IO事件分派后，对跨线程调用runInLoop()和queueInLoop()发来的函数对象进行处理
    // 通过跨线程调用，自始至终只让一个线程执行，避免多个线程同时操作共享数据结构导致的data race问题
    // 同时也实现了无锁编程
    doPendingFunctors();
  }

  // 如果quit_在别的位置被置为false，循环就会结束
  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

// 结束事件循环
void EventLoop::quit()
{
  // 将quit_标志位设置为true，注意这里可能并不是在IO线程中调用的，可能是在其他线程调用的
  // 如果不是在IO线程调用的，那么IO线程就有可能阻塞在poll()调用中，此时需要通过wakeup()
  // 唤醒该线程，唤醒之后会分派IO事件，然后执行doPendingFunctors()，之后循环退出
  // 所以quit()并不能立马见效
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  if (!isInLoopThread())
  {
    wakeup();
  }
}

// 在IO线程中执行某个函数，即跨线程调用
void EventLoop::runInLoop(Functor cb)
{
  // 若当前就是IO线程，直接执行
  if (isInLoopThread())
  {
    cb();
  }
  else // 否则将其塞入pendingFunctors_
  {
    queueInLoop(std::move(cb));
  }
}

// 将函数对象塞入pendingFunctors_，之后调用wakeup()唤醒IO线程
// 使得塞入的函数对象能尽可能早的得到执行，降低延迟
void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb));
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

// 返回现在到底有多少函数对象还在队列中没有得到执行
size_t EventLoop::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return pendingFunctors_.size();
}

// 定时器队列添加任务，在某时执行某函数
TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

// 定时器队列添加任务，在某个时延之后执行某函数
TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

// 定时器队列添加任务，每隔一段时间执行一次某函数
TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

// 取消定时任务
void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

// 更新channel，实际上调用poller_进行channel更新
void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

// 从EventLoop删除channel
void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  // 如果当前正在进行IO事件分派，断言被删除的channel为当前的活动channel，或者channel不在activeChannels中
  // 否则多个线程操作同一个数据结构，会data race，实际上只要removeChannel()函数在IO线程中调用就不会
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

// 判断EventLoop中是否注册了这个channel
bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

// 断言不在IO事件循环线程中，退出
void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

// 在用户态向wakeupFd_写入8字节的数据，因为wakeupFd_和wakeupChannel_是一起被注册到
// 该事件循环中的，因此poll()调用会立即返回，然后EventLoop会分派读事件到它自己的handleRead()函数上
// 下面将要分析的handleRead()函数会读取8个字节的数据
void EventLoop::wakeup()
{
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

// handleRead()函数，当wakeup()唤醒线程时被调用，读取写入的8字节数据
void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

// 执行跨线程调用传来的函数对象
void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

// 将pendingFunctors_中的函数对象交换到局部变量functors中，避免长时间锁住共享数据
  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
  }

// 循环执行局部变量中的函数对象
  for (const Functor& functor : functors)
  {
    functor();
  }
  callingPendingFunctors_ = false;
}

// 打印所有的活动channel，方便跟踪调试
void EventLoop::printActiveChannels() const
{
  for (const Channel* channel : activeChannels_)
  {
    LOG_TRACE << "{" << channel->reventsToString() << "} ";
  }
}

