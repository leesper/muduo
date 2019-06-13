// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_EPOLLPOLLER_H
#define MUDUO_NET_POLLER_EPOLLPOLLER_H

#include "muduo/net/Poller.h"

#include <vector>

struct epoll_event;

namespace muduo
{
namespace net
{

///
/// IO Multiplexing with epoll(4).
///
class EPollPoller : public Poller
{
 public:
 // 构造函数和析构函数
  EPollPoller(EventLoop* loop);
  ~EPollPoller() override;

// 还是那个关键的核心函数
  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
  // 更新channel以及删除channel
  void updateChannel(Channel* channel) override;
  void removeChannel(Channel* channel) override;

 private:
 // 初始事件列表长度
  static const int kInitEventListSize = 16;
// 把操作码转换成方便人读的字符串
  static const char* operationToString(int op);
// 用所有触发了IO事件的channel来填充activeChannels
  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;
  // 更新操作
  void update(int operation, Channel* channel);
// epoll_event事件列表
  typedef std::vector<struct epoll_event> EventList;
// 向内核注册的时候得到的描述符，epoll_create()
  int epollfd_;
  // epoll_event事件列表
  EventList events_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_POLLER_EPOLLPOLLER_H
