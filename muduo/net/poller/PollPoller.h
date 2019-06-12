// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_POLLPOLLER_H
#define MUDUO_NET_POLLER_POLLPOLLER_H

#include "muduo/net/Poller.h"

#include <vector>

struct pollfd;

namespace muduo
{
namespace net
{

///
/// IO Multiplexing with poll(2).
///
class PollPoller : public Poller
{
 public:

// 这是使用poll的Poller噢！
  PollPoller(EventLoop* loop);
  // 析构
  ~PollPoller() override;

// 阻塞等待IO事件发生的关键函数
  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
  // 更新channel
  void updateChannel(Channel* channel) override;
  // 删除channel
  void removeChannel(Channel* channel) override;

 private:
 // 把找到的活动channel塞给activeChannels
  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;

// 所有注册进内核的struct pollfd
  typedef std::vector<struct pollfd> PollFdList;
  PollFdList pollfds_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_POLLER_POLLPOLLER_H
