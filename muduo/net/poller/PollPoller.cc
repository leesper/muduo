// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/poller/PollPoller.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Channel.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

// 构造函数，其实啥都没做只是把loop通过父类构造函数赋值给了ownerLoop_
PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller() = default;

// 大名鼎鼎的核心函数
Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  // 阻塞等待IO事件的发生
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happened";
    // 所有发生了IO事件的channels都塞到activeChannels里面
    fillActiveChannels(numEvents, activeChannels);
  }
  else if (numEvents == 0) // 什么事都没发生噢~
  {
    LOG_TRACE << " nothing happened";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()"; // 出现了奇怪的错误
    }
  }
  return now;
}

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  // 遍历所有的pollfds
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
    // 若的确有事件发生
    if (pfd->revents > 0)
    {
      --numEvents;
      // 找到对应的channel
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      // 设置它收到的事件
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      // 放到activeChannels中
      activeChannels->push_back(channel);
    }
  }
}

void PollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0) // 说明是个新来的，还没index
  {
    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    // 初始化struct pollfd结构
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    // 放到pollfds_中
    pollfds_.push_back(pfd);
    // 分配一个索引
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);
    // 放进ChannelMap
    channels_[pfd.fd] = channel;
  }
  else
  {
    // update existing one
    // 这家伙已经存在了
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    // 那就通过索引找到它
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    // 初始化，主要是fd，events和revents
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    // 如果什么事件都不关心，那就给fd设置一个相反数减一
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
      pfd.fd = -channel->fd()-1;
    }
  }
}


void PollPoller::removeChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  // 要被删除的家伙是不是真的在且关闭了所有的感兴趣的事件呢，先检查下
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent());
  // 通过索引找到对应的pollfd
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  // 从channels中删掉它
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;
  // 如果这家伙恰好排在最后，直接pop出来
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
    pollfds_.pop_back();
  }
  else
  {
    // 否则让它和最后一个元素交换（swap），然后仍然pop掉，记得更新索引噢！
    int channelAtEnd = pollfds_.back().fd;
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0)
    {
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx);
    pollfds_.pop_back();
  }
}

