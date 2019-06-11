// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/Timer.h"

using namespace muduo;
using namespace muduo::net;

AtomicInt64 Timer::s_numCreated_;

void Timer::restart(Timestamp now)
{
  // 如果是可重复的任务，我们就把下次执行的时间放在now+interval
  if (repeat_)
  {
    expiration_ = addTime(now, interval_);
  }
  else
  {
    // 否则就设置为现在
    expiration_ = Timestamp::invalid();
  }
}
