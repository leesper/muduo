// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"

#include <sstream>

#include <poll.h>

using namespace muduo;
using namespace muduo::net;

// 表示该channel对任何事件都不感兴趣
const int Channel::kNoneEvent = 0;
// 对读事件感兴趣
const int Channel::kReadEvent = POLLIN | POLLPRI;
// 对写事件感兴趣
const int Channel::kWriteEvent = POLLOUT;

// 构造函数
Channel::Channel(EventLoop* loop, int fd__)
  : loop_(loop), // Channel所属事件循环
    fd_(fd__), // 文件描述符
    events_(0), // 感兴趣的事件默认为0
    revents_(0), // 获取的结果事件
    index_(-1), // 索引
    logHup_(true), 
    tied_(false), // not tied
    eventHandling_(false), // 默认没有处理事件
    addedToLoop_(false) // 默认还没有加入到事件循环
{
}

// 析构函数
Channel::~Channel()
{
  assert(!eventHandling_); // 断言没有在分派事件
  assert(!addedToLoop_); // 断言该channel已经不在事件循环中了
  if (loop_->isInLoopThread()) // 若现在就在事件循环所属线程中
  {
    assert(!loop_->hasChannel(this)); // 断言channel不在loop中
  }
}

// 绑定对象
void Channel::tie(const std::shared_ptr<void>& obj)
{
  tie_ = obj;
  tied_ = true;
}

// 更新该channel，设置addedToLoop_为true，然后调用所属loop_的updateChannel()方法
void Channel::update()
{
  addedToLoop_ = true;
  loop_->updateChannel(this);
}

// 从loop_中删除这个channel
void Channel::remove()
{
  assert(isNoneEvent());
  addedToLoop_ = false;
  loop_->removeChannel(this);
}

// 分派IO事件，回调用户注册的函数，若绑定过就调用绑定对象的lock，先锁住再执行分派，否则直接分派
void Channel::handleEvent(Timestamp receiveTime)
{
  std::shared_ptr<void> guard;
  if (tied_)
  {
    guard = tie_.lock();
    if (guard)
    {
      handleEventWithGuard(receiveTime);
    }
  }
  else
  {
    handleEventWithGuard(receiveTime);
  }
}

// 真正的事件分派函数，只在IO线程中执行
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
  // eventHandling_为true，表示正在执行IO分派
  eventHandling_ = true;
  // 打印日志
  LOG_TRACE << reventsToString();

  // 若捕捉到POLLHUP事件，说明对方已经close，调用closeCallback
  // 这里之所以还要判断一下revents_ 中没有POLLIN，是需要保证管道上的数据
  // 已经读完了，这时候才能放心地closeCallback()
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
  {
    if (logHup_)
    {
      LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
    }
    if (closeCallback_) closeCallback_();
  }

// 无效请求，fd没有open()
  if (revents_ & POLLNVAL)
  {
    LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
  }

// 出错了，调用errorCallback()
  if (revents_ & (POLLERR | POLLNVAL))
  {
    if (errorCallback_) errorCallback_();
  }
// 有数据可读/有带外数据/对方关闭了write half，回调注册的读函数
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
  {
    if (readCallback_) readCallback_(receiveTime);
  }
  // 内核缓冲区有多余的空间，那就写呗
  if (revents_ & POLLOUT)
  {
    if (writeCallback_) writeCallback_();
  }
  // 处理完了，恢复到false
  eventHandling_ = false;
}

// 以下函数将channel上的事件解析成可读性好的字符串
string Channel::reventsToString() const
{
  return eventsToString(fd_, revents_);
}

string Channel::eventsToString() const
{
  return eventsToString(fd_, events_);
}

string Channel::eventsToString(int fd, int ev)
{
  std::ostringstream oss;
  oss << fd << ": ";
  if (ev & POLLIN)
    oss << "IN ";
  if (ev & POLLPRI)
    oss << "PRI ";
  if (ev & POLLOUT)
    oss << "OUT ";
  if (ev & POLLHUP)
    oss << "HUP ";
  if (ev & POLLRDHUP)
    oss << "RDHUP ";
  if (ev & POLLERR)
    oss << "ERR ";
  if (ev & POLLNVAL)
    oss << "NVAL ";

  return oss.str();
}
