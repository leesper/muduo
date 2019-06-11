// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Timestamp.h"

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

// 前置声明，这样就不用引入EvenLoop头文件咯
class EventLoop;

///
/// A selectable I/O channel.
///
/// This class doesn't own the file descriptor.
/// The file descriptor could be a socket,
/// an eventfd, a timerfd, or a signalfd
// 其实就是POSA2里面讲到的那个EventHandler，和fd一一对应，
// 它用来分派fd上的IO事件到对应的事件处理函数
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback;
  typedef std::function<void(Timestamp)> ReadEventCallback;

// 构造函数
  Channel(EventLoop* loop, int fd);
// 析构函数
  ~Channel();

// 分派IO事件，回调相应处理函数的关键代码
  void handleEvent(Timestamp receiveTime);
// 设置读回调
  void setReadCallback(ReadEventCallback cb)
  { readCallback_ = std::move(cb); }
// 设置写回调
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
// 设置关闭时的回调
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
// 设置出错时的回调
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  void tie(const std::shared_ptr<void>&);

  // 返回channel对应的文件描述符
  int fd() const { return fd_; }
  // 返回感兴趣的事件掩码
  int events() const { return events_; }
  // 设置poller返回的事件掩码
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  // 判断目前该channel是不是对所有事件都屏蔽了
  bool isNoneEvent() const { return events_ == kNoneEvent; }

// 打开开关响应读事件，update()将更新的事件注册到内核对应数据结构中，下同
  void enableReading() { events_ |= kReadEvent; update(); }
// 关闭开关忽略读事件
  void disableReading() { events_ &= ~kReadEvent; update(); }
// 打开开关响应写事件
  void enableWriting() { events_ |= kWriteEvent; update(); }
// 关闭开关忽略写事件
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
// 关闭所有开关忽略所有事件
  void disableAll() { events_ = kNoneEvent; update(); }
  // 对写事件感兴趣
  bool isWriting() const { return events_ & kWriteEvent; }
  // 对读事件感兴趣
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller 为Poller准备的索引值
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug 将事件信息转换为字符串方便跟踪调试
  string reventsToString() const;
  string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

// 返回channel绑定的EventLoop
  EventLoop* ownerLoop() { return loop_; }
  // 从EventLoop中删除自己
  void remove();

 private:
  static string eventsToString(int fd, int ev);

// 在EventLoop中更新自己的状态，通过poller传递到内核
  void update();
// 在弱指针的保护下分派IO事件回调用户注册的函数，弱指针保护owner对象不在handleEvent()中被destroy掉
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_; // owner事件循环
  const int  fd_; // 对应文件描述符
  int        events_; // 注册的事件
  int        revents_; // it's the received event types of epoll or poll
  int        index_; // used by Poller.
  bool       logHup_;

  std::weak_ptr<void> tie_; //弱指针引用对象
  bool tied_; // tie标志位
  bool eventHandling_; // 正在分派IO事件标志位
  bool addedToLoop_; // 该channel已绑定到EventLoop标志位
  ReadEventCallback readCallback_; // 读回调
  EventCallback writeCallback_; // 写回调
  EventCallback closeCallback_; // 关闭回调
  EventCallback errorCallback_; // 出错回调
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
