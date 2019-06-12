// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_H
#define MUDUO_NET_POLLER_H

#include <map>
#include <vector>

#include "muduo/base/Timestamp.h"
#include "muduo/net/EventLoop.h"

namespace muduo
{
namespace net
{

class Channel;

///
/// Base class for IO Multiplexing
///
/// This class doesn't own the Channel objects.
// 同步IO事件分派器
class Poller : noncopyable
{
 public:
 // 定义ChannelList类型，其实就是一个数组而已
  typedef std::vector<Channel*> ChannelList;

  // Poller的构造函数，肯定是要传EventLoop的啦
  Poller(EventLoop* loop);
  // 虚析构函数，哈哈，什么意思我都想不起来咯
  virtual ~Poller();

  /// Polls the I/O events.
  /// Must be called in the loop thread.
  // EventLoop中的事件循环loop()就是靠它阻塞地等待注册的IO事件，然后执行分派
  virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;

  /// Changes the interested I/O events.
  /// Must be called in the loop thread.
  // 更新channel
  virtual void updateChannel(Channel* channel) = 0;

  /// Remove the channel, when it destructs.
  /// Must be called in the loop thread.
  // 删除channel
  virtual void removeChannel(Channel* channel) = 0;

// 这个channel是我的吗
  virtual bool hasChannel(Channel* channel) const;

// 创建一个默认的Poller给我，工厂方法
  static Poller* newDefaultPoller(EventLoop* loop);

// 我在IO线程中吗
  void assertInLoopThread() const
  {
    ownerLoop_->assertInLoopThread();
  }

 protected:
 // fd和对应channel的map
  typedef std::map<int, Channel*> ChannelMap;
  ChannelMap channels_;

 private:
 // 这是我的owner
  EventLoop* ownerLoop_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_POLLER_H
